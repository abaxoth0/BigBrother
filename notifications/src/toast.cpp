#include "toast.h"
#include "transport.h"
#include <shobjidl.h>
#include <atomic>

namespace notifications {
using Microsoft::WRL::ComPtr;
using namespace ABI::Windows::UI::Notifications;
using namespace ABI::Windows::Data::Xml::Dom;
using failed_handler = ABI::Windows::Foundation::ITypedEventHandler<ToastNotification*, ToastFailedEventArgs*>;

class toast_failure final : public failed_handler, public IAgileObject {
    std::atomic<ULONG> references_{1};
public:
    handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    HRESULT result = E_FAIL;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (id == IID_IUnknown || id == __uuidof(failed_handler)) *object = static_cast<failed_handler*>(this);
        else if (id == IID_IAgileObject) *object = static_cast<IAgileObject*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG remaining = --references_;
        if (!remaining) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE Invoke(IToastNotification*, IToastFailedEventArgs* args) override {
        args->get_ErrorCode(&result);
        SetEvent(event.get());
        return S_OK;
    }
};

void show_toast(const product& app, const std::wstring& title, const std::wstring& body) {
    BB_TRACE(L"Preparing notification identity: " + std::wstring(app.app_id));
    ensure_registration(app);
    check(SetCurrentProcessExplicitAppUserModelID(app.app_id), L"Set notification identity");
    ComPtr<IToastNotificationManagerStatics> manager;
    check(RoGetActivationFactory(hstring(L"Windows.UI.Notifications.ToastNotificationManager").get(),
        IID_PPV_ARGS(&manager)), L"Activate WinRT toast manager");
    ComPtr<IToastNotifier> notifier;
    check(manager->CreateToastNotifierWithId(hstring(app.app_id).get(), &notifier), L"Create WinRT toast notifier");
    BB_TRACE(L"WinRT toast notifier created");
    ComPtr<IInspectable> instance;
    check(RoActivateInstance(hstring(L"Windows.Data.Xml.Dom.XmlDocument").get(), &instance), L"Activate WinRT XML document");
    ComPtr<IXmlDocument> document;
    check(instance.As(&document), L"Query WinRT XML document");
    ComPtr<IXmlDocumentIO> xml;
    check(document.As(&xml), L"Query WinRT XML loader");
    check(xml->LoadXml(hstring(toast_xml(app.uri(), title, body)).get()), L"Load toast XML");
    BB_TRACE(L"Toast XML loaded successfully");
    ComPtr<IToastNotificationFactory> factory;
    check(RoGetActivationFactory(hstring(L"Windows.UI.Notifications.ToastNotification").get(),
        IID_PPV_ARGS(&factory)), L"Activate WinRT toast factory");
    ComPtr<IToastNotification> toast;
    check(factory->CreateToastNotification(document.Get(), &toast), L"Create WinRT toast");
    submit_toast(notifier.Get(), toast.Get());
}

void submit_toast(IToastNotifier* notifier, IToastNotification* toast) {
    NotificationSetting setting;
    HRESULT settings_result = notifier->get_Setting(&setting);
    if (SUCCEEDED(settings_result)) {
        switch (setting) {
            case NotificationSetting_Enabled: break;
            case NotificationSetting_DisabledForApplication:
                throw failure{E_ACCESSDENIED, L"Windows notifications disabled for this application"};
            case NotificationSetting_DisabledForUser:
                throw failure{E_ACCESSDENIED, L"Windows notifications disabled for this user"};
            case NotificationSetting_DisabledByGroupPolicy:
                throw failure{E_ACCESSDENIED, L"Windows notifications disabled by group policy"};
            case NotificationSetting_DisabledByManifest:
                throw failure{E_ACCESSDENIED, L"Windows notifications disabled by manifest"};
            default:
                print_message(L"Windows returned an unknown notification setting; attempting submission");
        }
    } else {
        wchar_t message[128];
        swprintf(message, ARRAYSIZE(message), L"Notification settings unavailable (0x%08lX); attempting submission",
            static_cast<unsigned long>(settings_result));
        print_message(message);
    }
    ComPtr<toast_failure> failed;
    failed.Attach(new toast_failure());
    check_win32(failed->event.get() != nullptr, L"Create toast result event");
    EventRegistrationToken token{};
    check(toast->add_Failed(failed.Get(), &token), L"Observe toast delivery failures");
    BB_TRACE(L"Calling IToastNotifier::Show");
    HRESULT result = notifier->Show(toast);
    if (SUCCEEDED(result)) {
        BB_TRACE(L"Show returned S_OK; waiting for asynchronous delivery errors");
        DWORD wait = WaitForSingleObject(failed->event.get(), 5000);
        if (wait == WAIT_OBJECT_0) result = failed->result;
        else if (wait == WAIT_FAILED) result = HRESULT_FROM_WIN32(GetLastError());
        else print_message(L"Submitted to Windows; no failure reported within 5 seconds. Banner display is not confirmed.");
    }
    toast->remove_Failed(token);
    check(result, L"Show WinRT toast");
}
}
