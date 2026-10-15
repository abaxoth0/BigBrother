#include "app.h"
#include "transport.h"
#include "command_line.h"
#include <wtsapi32.h>
#include <userenv.h>
#include <shellapi.h>
#include <shlobj.h>
#include <cstdio>
#include <vector>

#define BB_WIDEN_LITERAL(value) L##value
#define BB_WIDEN(value) BB_WIDEN_LITERAL(value)

namespace notifications {

bool is_system() {
    handle token;
    check_win32(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, token.put()), L"Open process token");
    alignas(TOKEN_USER) BYTE buffer[256];
    DWORD needed;
    check_win32(GetTokenInformation(token.get(), TokenUser, buffer, sizeof(buffer), &needed), L"Read process identity");
    return IsWellKnownSid(reinterpret_cast<TOKEN_USER*>(buffer)->User.Sid, WinLocalSystemSid);
}

static handle launch_in_session(const product& app, const std::wstring& title_hex, const std::wstring& body_hex, HANDLE token) {
    auto path = executable_path();
    std::wstring command = L"\"" + path + L"\" --show " + app.name + L" " + title_hex + L" " + body_hex;
    STARTUPINFOW startup{};
    PROCESS_INFORMATION process{};
    startup.cb = sizeof(startup);
    wchar_t desktop[] = L"winsta0\\default";
    startup.lpDesktop = desktop;
    LPVOID environment = nullptr;
    check_win32(CreateEnvironmentBlock(&environment, token, FALSE), L"Create user environment");
    BOOL created = CreateProcessAsUserW(token, path.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW, environment, app_directory().c_str(), &startup, &process);
    DWORD error = GetLastError();
    DestroyEnvironmentBlock(environment);
    check(HRESULT_FROM_WIN32(created ? ERROR_SUCCESS : error), L"Launch user notification process");
    CloseHandle(process.hThread);
    return handle(process.hProcess);
}

static DWORD send_notification(const product& app, const std::wstring& title_hex, const std::wstring& body_hex) {
    auto title = decode(title_hex, 256), body = decode(body_hex, 1024);
    DWORD session;
    check_win32(ProcessIdToSessionId(GetCurrentProcessId(), &session), L"Read process session");
    bool system = is_system();
    trace(L"Delivery context: session=" + std::to_wstring(session) + (system ? L", SYSTEM" : L", user"));
    if (session && !system) {
        check(RoInitialize(RO_INIT_MULTITHREADED), L"Initialize WinRT");
        struct uninitialize { ~uninitialize() { RoUninitialize(); } } apartment;
        show_toast(app, title, body);
        return ERROR_SUCCESS;
    }

    handle token;
    check_win32(OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, token.put()), L"Open service token");
    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    check_win32(LookupPrivilegeValueW(nullptr, SE_TCB_NAME, &privileges.Privileges[0].Luid), L"Find session privilege");
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    SetLastError(ERROR_SUCCESS);
    check_win32(AdjustTokenPrivileges(token.get(), FALSE, &privileges, 0, nullptr, nullptr), L"Enable session privilege");
    check(HRESULT_FROM_WIN32(GetLastError()), L"Acquire session privilege");
    struct session_list {
        WTS_SESSION_INFOW* data = nullptr;
        ~session_list() { if (data) WTSFreeMemory(data); }
    } sessions;
    DWORD count;
    check_win32(WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessions.data, &count), L"Enumerate user sessions");
    std::vector<handle> processes;
    DWORD result = ERROR_SUCCESS;
    for (DWORD i = 0; i < count; i++) {
        if (!sessions.data[i].SessionId || sessions.data[i].State != WTSActive) continue;
        try {
            handle user;
            check_win32(WTSQueryUserToken(sessions.data[i].SessionId, user.put()), L"Read signed-in user token");
            processes.push_back(launch_in_session(app, title_hex, body_hex, user.get()));
        } catch (const failure& error) { result = report_failure(error); }
    }
    if (processes.empty() && result == ERROR_SUCCESS && diagnostics_enabled())
        throw failure{HRESULT_FROM_WIN32(ERROR_NO_SUCH_LOGON_SESSION), L"No active signed-in user session for the test notification"};
    trace(L"User-session delivery processes: " + std::to_wstring(processes.size()));
    ULONGLONG deadline = GetTickCount64() + 8000;
    for (auto& process : processes) {
        ULONGLONG now = GetTickCount64();
        DWORD remaining = static_cast<DWORD>(deadline > now ? deadline - now : 0);
        if (WaitForSingleObject(process.get(), remaining) != WAIT_OBJECT_0) {
            TerminateProcess(process.get(), ERROR_TIMEOUT);
            result = report_failure({HRESULT_FROM_WIN32(ERROR_TIMEOUT), L"Wait for user notification result"});
            continue;
        }
        DWORD code;
        check_win32(GetExitCodeProcess(process.get(), &code), L"Read user notification result");
        if (code) result = report_failure({static_cast<HRESULT>(code), L"User notification delivery"});
    }
    return result;
}

static void open_frontend(const product& app) {
    auto path = app_directory() + L"\\GUI\\" + app.display_name() + L".exe";
    auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr,
        (app_directory() + L"\\GUI").c_str(), SW_SHOWNORMAL));
    if (result <= 32) throw failure{HRESULT_FROM_WIN32(static_cast<DWORD>(result)), L"Open frontend"};
}

static DWORD run(const command& request) {
    const auto& app = request.app;
    if (request.kind == command_kind::help) {
        trace(L"Usage: BigBrother Notifications.exe [--show|--test [Client|Server]]; --register Client|Server; --unregister Client|Server; --version");
        return ERROR_SUCCESS;
    }
    if (request.kind == command_kind::version) return ERROR_SUCCESS;
    if (request.kind == command_kind::test)
        return send_notification(app, encode("BigBrother", 256), encode("Проверка уведомлений Windows", 1024));
    if (request.kind == command_kind::send) return send_notification(app, request.title, request.body);
    if (is_system()) throw failure{E_ACCESSDENIED, L"Display and registration require a signed-in user"};
    if (request.kind == command_kind::open) { open_frontend(app); return ERROR_SUCCESS; }
    check(RoInitialize(RO_INIT_MULTITHREADED), L"Initialize WinRT");
    struct uninitialize { ~uninitialize() { RoUninitialize(); } } apartment;
    if (request.kind == command_kind::show) show_toast(app, decode(request.title, 256), decode(request.body, 1024));
    else if (request.kind == command_kind::register_user) ensure_registration(app, true);
    else if (request.kind == command_kind::unregister_user) unregister_user(app);
    return ERROR_SUCCESS;
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    wchar_t** args = CommandLineToArgvW(GetCommandLineW(), &argc);
    DWORD command_error = args ? ERROR_SUCCESS : GetLastError();
    DWORD result;
    bool console_output = notifications::attach_console_output();
    bool manual = args && notifications::manual_test_command(argc, args);
    try {
        if (!args) throw notifications::failure{HRESULT_FROM_WIN32(command_error ? command_error : ERROR_GEN_FAILURE), L"Read notification command"};
        auto request = notifications::parse_command(argc, args);
        notifications::set_diagnostics(manual || request.kind == notifications::command_kind::help || request.kind == notifications::command_kind::version);
        notifications::trace(L"BigBrother Notifications build " BB_WIDEN(__DATE__) L" " BB_WIDEN(__TIME__));
        notifications::trace(L"Executable: " + notifications::executable_path());
        if (manual) notifications::trace(L"Manual test command accepted for " + request.app.display_name());
        result = notifications::run(request);
        if (manual && result == ERROR_SUCCESS)
            notifications::trace(L"Test request submitted to Windows; banner display is not confirmed. See the delivery stages above.");
    } catch (const notifications::failure& error) { result = notifications::report_failure(error); }
    catch (const std::invalid_argument&) { result = notifications::report_failure({E_INVALIDARG, L"Invalid UTF-8 notification payload"}); }
    catch (...) { result = notifications::report_failure({E_FAIL, L"Notification helper"}); }
    if (manual && !console_output && result) {
        try {
            DWORD session = 0;
            if (ProcessIdToSessionId(GetCurrentProcessId(), &session) && session && !notifications::is_system()) {
                wchar_t message[512];
                swprintf(message, ARRAYSIZE(message),
                    L"The test notification could not be sent (0x%08lX).\n\nDetails: %%LOCALAPPDATA%%\\BigBrother\\notifications.log", result);
                MessageBoxW(nullptr, message, L"BigBrother notification test", MB_OK | MB_ICONERROR);
            }
        } catch (...) {}
    }
    if (args) LocalFree(args);
    return static_cast<int>(result);
}
