#include "notify.h"
#include "transport.h"
#include "process.h"
#include <atomic>
#include <memory>

namespace {
std::atomic<bb_notification_error_handler> error_handler{nullptr};
volatile LONG active_deliveries = 0;

struct delivery {
    notifications::process_group group;
    notifications::handle process;
};

DWORD WINAPI await_delivery(void* context) {
    std::unique_ptr<delivery> pending(static_cast<delivery*>(context));
    DWORD result = ERROR_GEN_FAILURE;
    if (WaitForSingleObject(pending->process.get(), 35000) == WAIT_OBJECT_0)
        GetExitCodeProcess(pending->process.get(), &result);
    else {
        result = ERROR_TIMEOUT;
        TerminateProcess(pending->process.get(), result);
    }
    if (result) {
        if (auto handler = error_handler.load()) handler(result);
        else OutputDebugStringW(L"BigBrother: notification delivery failed\n");
    }
    InterlockedDecrement(&active_deliveries);
    return 0;
}
}

extern "C" void bb_notifications_set_error_handler(bb_notification_error_handler handler) {
    error_handler.store(handler);
}

extern "C" int bb_notify(const char* title, const char* message) {
    if (!title || !message) { SetLastError(ERROR_INVALID_PARAMETER); return -1; }
    try {
        std::wstring path(32768, 0);
        DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!length || length == path.size()) return -1;
        path.resize(length);
        auto separator = path.find_last_of(L'\\');
        if (separator == path.npos) { SetLastError(ERROR_BAD_PATHNAME); return -1; }
        path.resize(separator + 1);
        path += L"BigBrother Notifications.exe";
        std::wstring command = L"\"" + path + L"\" --send Client " + notifications::encode(title, 256) +
            L" " + notifications::encode(message, 1024);
        auto pending = std::make_unique<delivery>();
        if (InterlockedIncrement(&active_deliveries) > 16) {
            InterlockedDecrement(&active_deliveries);
            SetLastError(ERROR_BUSY);
            return -1;
        }
        struct active_guard {
            bool armed = true;
            ~active_guard() { if (armed) InterlockedDecrement(&active_deliveries); }
        } active;
        STARTUPINFOW startup{};
        PROCESS_INFORMATION process{};
        startup.cb = sizeof(startup);
        if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
            nullptr, nullptr, &startup, &process)) {
            return -1;
        }
        auto child = pending->group.resume(process);
        *pending->process.put() = child.get();
        *child.put() = nullptr;
        HANDLE waiter = CreateThread(nullptr, 0, await_delivery, pending.get(), 0, nullptr);
        if (!waiter) {
            DWORD error = GetLastError();
            TerminateProcess(pending->process.get(), error);
            SetLastError(error);
            return -1;
        }
        active.armed = false;
        pending.release();
        CloseHandle(waiter);
        return 0;
    } catch (const notifications::failure& error) {
        SetLastError(HRESULT_CODE(error.code));
    } catch (const std::invalid_argument&) {
        SetLastError(ERROR_INVALID_PARAMETER);
    } catch (...) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    }
    return -1;
}
