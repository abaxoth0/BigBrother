#include "toast.h"
#include <cassert>
#include <thread>
#include <chrono>

namespace {
using namespace ABI::Windows::UI::Notifications;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Data::Xml::Dom;
using failed_handler = ITypedEventHandler<ToastNotification*, ToastFailedEventArgs*>;

template<class Interface> class fake_inspectable : public Interface {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (id != IID_IUnknown && id != __uuidof(IInspectable) && id != __uuidof(Interface)) return E_NOINTERFACE;
        *object = static_cast<Interface*>(this);
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG*, IID**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel*) override { return E_NOTIMPL; }
};

class fake_toast final : public fake_inspectable<IToastNotification> {
public:
    failed_handler* failed = nullptr;
    HRESULT STDMETHODCALLTYPE get_Content(IXmlDocument**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE put_ExpirationTime(IReference<DateTime>*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_ExpirationTime(IReference<DateTime>**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE add_Dismissed(ITypedEventHandler<ToastNotification*, ToastDismissedEventArgs*>*, EventRegistrationToken*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE remove_Dismissed(EventRegistrationToken) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE add_Activated(ITypedEventHandler<ToastNotification*, IInspectable*>*, EventRegistrationToken*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE remove_Activated(EventRegistrationToken) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE add_Failed(failed_handler* handler, EventRegistrationToken* token) override {
        assert(!failed);
        failed = handler;
        failed->AddRef();
        token->value = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE remove_Failed(EventRegistrationToken) override {
        failed->Release();
        failed = nullptr;
        return S_OK;
    }
};

class failure_args final : public fake_inspectable<IToastFailedEventArgs> {
public:
    HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* error) override { *error = E_FAIL; return S_OK; }
};

class fake_notifier final : public fake_inspectable<IToastNotifier> {
public:
    int settings_reads = 0;
    int submissions = 0;
    HRESULT result = S_OK;
    bool asynchronous_failure = false;
    DWORD failure_delay = 0;
    std::thread callback;
    HRESULT settings_result = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    NotificationSetting setting = NotificationSetting_Enabled;
    ~fake_notifier() { if (callback.joinable()) callback.join(); }
    HRESULT STDMETHODCALLTYPE Show(IToastNotification* toast) override {
        submissions++;
        if (asynchronous_failure) {
            auto handler = static_cast<fake_toast*>(toast)->failed;
            handler->AddRef();
            callback = std::thread([handler, toast, delay = failure_delay] {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                failure_args args;
                handler->Invoke(toast, &args);
                handler->Release();
            });
        }
        return result;
    }
    HRESULT STDMETHODCALLTYPE Hide(IToastNotification*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_Setting(NotificationSetting* value) override {
        settings_reads++;
        *value = setting;
        return settings_result;
    }
    HRESULT STDMETHODCALLTYPE AddToSchedule(IScheduledToastNotification*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RemoveFromSchedule(IScheduledToastNotification*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetScheduledToastNotifications(Collections::IVectorView<ScheduledToastNotification*>**) override { return E_NOTIMPL; }
};
}

void delivery_tests() {
    fake_notifier notifier;
    fake_toast toast;
    notifications::submit_toast(&notifier, &toast);
    assert(notifier.submissions == 1 && notifier.settings_reads == 1 && !toast.failed);
    notifier.result = E_ACCESSDENIED;
    bool failed = false;
    try { notifications::submit_toast(&notifier, &toast); }
    catch (const notifications::failure& error) { failed = error.code == E_ACCESSDENIED; }
    assert(failed && !toast.failed);
    notifier.result = S_OK;
    notifier.asynchronous_failure = true;
    notifier.failure_delay = 1500;
    failed = false;
    try { notifications::submit_toast(&notifier, &toast); }
    catch (const notifications::failure& error) { failed = error.code == E_FAIL; }
    assert(failed && !toast.failed && notifier.settings_reads == 3);
    notifier.callback.join();
    notifier.settings_result = S_OK;
    for (auto setting : {NotificationSetting_DisabledForApplication, NotificationSetting_DisabledForUser,
                        NotificationSetting_DisabledByGroupPolicy, NotificationSetting_DisabledByManifest}) {
        notifier.setting = setting;
        failed = false;
        try { notifications::submit_toast(&notifier, &toast); }
        catch (const notifications::failure& error) { failed = error.code == E_ACCESSDENIED; }
        assert(failed && !toast.failed && notifier.submissions == 3);
    }
}
