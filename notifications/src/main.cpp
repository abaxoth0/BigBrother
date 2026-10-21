#include "app.h"
#include "transport.h"
#include "command_line.h"
#include "version.h"
#include "report.h"
#include <memory>
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

static handle launch_in_session(delivery_report& report, ULONGLONG deadline, const product& app, const std::wstring& title_hex, const std::wstring& body_hex, HANDLE token) {
    auto path = executable_path();
    std::wstring command = L"\"" + path + L"\" --show " + app.name + L" " + title_hex + L" " + body_hex + L" --report " + report.name;
    STARTUPINFOW startup{};
    PROCESS_INFORMATION process{};
    startup.cb = sizeof(startup);
    wchar_t desktop[] = L"winsta0\\default";
    startup.lpDesktop = desktop;
    LPVOID environment = nullptr;
    check_win32(CreateEnvironmentBlock(&environment, token, FALSE), L"Create user environment");
    BOOL created = CreateProcessAsUserW(token, path.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_BREAKAWAY_FROM_JOB, environment, app_directory().c_str(), &startup, &process);
    DWORD error = GetLastError();
    DestroyEnvironmentBlock(environment);
    check(HRESULT_FROM_WIN32(created ? ERROR_SUCCESS : error), L"Launch user notification process");
    handle child(process.hProcess), thread(process.hThread);
    try {
        report.supervise(child.get(), deadline);
        check_win32(ResumeThread(thread.get()) != static_cast<DWORD>(-1), L"Resume user notification process");
    } catch (...) { TerminateProcess(child.get(), ERROR_CANCELLED); throw; }
    return child;
}

static DWORD send_notification(const product& app, const std::wstring& title_hex, const std::wstring& body_hex) {
    auto title = decode(title_hex, 256), body = decode(body_hex, 1024);
    DWORD session;
    check_win32(ProcessIdToSessionId(GetCurrentProcessId(), &session), L"Read process session");
    bool system = is_system();
    BB_TRACE(L"Delivery context: session=" + std::to_wstring(session) + (system ? L", SYSTEM" : L", user"));
    if (session && !system) {
        check(RoInitialize(RO_INIT_MULTITHREADED), L"Initialize WinRT");
        struct uninitialize { ~uninitialize() { RoUninitialize(); } } apartment;
        show_toast(app, title, body);
        return ERROR_SUCCESS;
    }

    ULONGLONG deadline = GetTickCount64() + 20000;
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
    DWORD count = 0;
    for (int attempt = 0; attempt < 3; attempt++) {
        check_win32(WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessions.data, &count), L"Enumerate user sessions");
        bool active = false;
        for (DWORD i = 0; i < count; i++)
            if (sessions.data[i].SessionId && sessions.data[i].State == WTSActive) active = true;
        if (active || attempt == 2) break;
        WTSFreeMemory(sessions.data);
        sessions.data = nullptr;
        Sleep(500);
    }
    struct recipient {
        std::unique_ptr<delivery_report> report;
        handle process;
        DWORD session;
    };
    std::vector<recipient> processes;
    DWORD result = ERROR_SUCCESS;
    for (DWORD i = 0; i < count; i++) {
        if (!sessions.data[i].SessionId || sessions.data[i].State != WTSActive) continue;
        try {
            handle user;
            BOOL available = WTSQueryUserToken(sessions.data[i].SessionId, user.put());
            if (!available && GetLastError() == ERROR_NO_TOKEN) {
                Sleep(250);
                available = WTSQueryUserToken(sessions.data[i].SessionId, user.put());
            }
            check_win32(available, L"Read signed-in user token");
            check(HRESULT_FROM_WIN32(GetTickCount64() < deadline ? ERROR_SUCCESS : ERROR_TIMEOUT), L"Notification delivery deadline");
            auto report = std::make_unique<delivery_report>(user.get());
            auto process = launch_in_session(*report, deadline, app, title_hex, body_hex, user.get());
            processes.push_back({std::move(report), std::move(process), sessions.data[i].SessionId});
        } catch (const failure& error) { result = report_failure(error); }
    }
    if (processes.empty() && result == ERROR_SUCCESS)
        throw failure{HRESULT_FROM_WIN32(ERROR_NO_SUCH_LOGON_SESSION), L"No active signed-in user session for notification delivery"};
    print_message(L"User-session delivery processes: " + std::to_wstring(processes.size()));
    for (auto& process : processes) {
        ULONGLONG now = GetTickCount64();
        DWORD remaining = static_cast<DWORD>(deadline > now ? deadline - now : 0);
        if (WaitForSingleObject(process.process.get(), remaining) != WAIT_OBJECT_0) {
            TerminateProcess(process.process.get(), ERROR_TIMEOUT);
            result = report_failure({HRESULT_FROM_WIN32(ERROR_TIMEOUT), L"Wait for user notification result"});
            continue;
        }
        DWORD code;
        check_win32(GetExitCodeProcess(process.process.get(), &code), L"Read user notification result");
        if (code) {
            auto detail = process.report->read();
            if (!detail.empty()) print_message(L"User session " + std::to_wstring(process.session) + L": " + detail);
        }
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
        print_message(L"Usage: BigBrother Notifications.exe [--show|--test [Client|Server]]; --register Client|Server; --unregister Client|Server; --version");
        return ERROR_SUCCESS;
    }
    if (request.kind == command_kind::version) {
        print_message(L"BigBrother Notifications " BB_WIDEN(BB_VERSION));
        return ERROR_SUCCESS;
    }
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
    std::unique_ptr<notifications::parent_watch> watchdog;
    try {
        if (!args) throw notifications::failure{HRESULT_FROM_WIN32(command_error ? command_error : ERROR_GEN_FAILURE), L"Read notification command"};
        if (argc == 7 && std::wstring_view(args[1]) == L"--show" && std::wstring_view(args[5]) == L"--report") {
            if (!notifications::report_name(args[6])) throw notifications::failure{E_INVALIDARG, L"Invalid notification result target"};
            notifications::set_report_target(args[6]);
            watchdog = std::make_unique<notifications::parent_watch>(args[6]);
            argc -= 2;
        }
        auto request = notifications::parse_command(argc, args);
        notifications::set_diagnostics(manual || request.kind == notifications::command_kind::help || request.kind == notifications::command_kind::version ||
            request.kind == notifications::command_kind::send || request.kind == notifications::command_kind::show);
        BB_TRACE(L"BigBrother Notifications " BB_WIDEN(BB_VERSION));
        BB_TRACE(L"BigBrother Notifications build " BB_WIDEN(__DATE__) L" " BB_WIDEN(__TIME__));
        BB_TRACE(L"Executable: " + notifications::executable_path());
        if (manual) BB_TRACE(L"Manual test command accepted for " + request.app.display_name());
        result = notifications::run(request);
        if (manual && result == ERROR_SUCCESS)
            BB_TRACE(L"Test request submitted to Windows; banner display is not confirmed. See the delivery stages above.");
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
