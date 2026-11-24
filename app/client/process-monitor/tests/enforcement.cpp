// Run beside BigBrother Process Monitor.exe from an interactive Windows session.
#include "resource.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>

using client::Handle;
namespace fs = std::filesystem;

static Handle desktop_token() {
    HANDLE raw = nullptr;
    assert(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &raw));
    Handle current(raw);
    DWORD bytes = 0;
    TOKEN_ELEVATION elevated{};
    assert(GetTokenInformation(raw, TokenElevation, &elevated, sizeof(elevated), &bytes));
    if (!elevated.TokenIsElevated) {
        return Handle();
    }
    TOKEN_LINKED_TOKEN linked{};
    assert(GetTokenInformation(raw, TokenLinkedToken, &linked, sizeof(linked), &bytes));
    return Handle(linked.LinkedToken);
}

static Handle start(HANDLE token, const fs::path& exe, const std::wstring& args) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring command = L"\"" + exe.wstring() + L"\" " + args;
    BOOL started = FALSE;
    if (token) {
        HANDLE raw = nullptr;
        assert(DuplicateTokenEx(token, MAXIMUM_ALLOWED, nullptr, SecurityImpersonation, TokenPrimary, &raw));
        Handle primary(raw);
        started = CreateProcessAsUserW(raw, exe.c_str(), command.data(), nullptr, nullptr, FALSE,
                                       CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    } else {
        started = CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                 nullptr, nullptr, &startup, &process);
    }
    if (!started) {
        std::fprintf(stderr, "Cannot start fixture process (%lu)\n", GetLastError());
        std::exit(1);
    }
    CloseHandle(process.hThread);
    return Handle(process.hProcess);
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring_view(argv[1]) == L"--wait") {
        Sleep(15000);
        return 0;
    }
    auto self = fs::absolute(argv[0]);
    auto prefix = L"bb-monitor-test-" + std::to_wstring(GetCurrentProcessId()) + L"-";
    auto blocked = fs::temp_directory_path() / (prefix + L"blocked.exe");
    auto allowed = fs::temp_directory_path() / (prefix + L"allowed.exe");
    fs::copy_file(self, blocked);
    fs::copy_file(self, allowed);
    auto token = desktop_token();
    auto blockedChild = start(token.get(), blocked, L"--wait");
    auto allowedChild = start(token.get(), allowed, L"--wait");
    auto monitor = start(token.get(), self.parent_path() / L"BigBrother Process Monitor.exe",
                         L"--rules \"" + prefix + L"*.exe\n!" + allowed.filename().wstring() + L"\"");
    bool stopped = WaitForSingleObject(blockedChild.get(), 5000) == WAIT_OBJECT_0;
    DWORD code = 0;
    GetExitCodeProcess(blockedChild.get(), &code);
    bool allowedRunning = WaitForSingleObject(allowedChild.get(), 0) == WAIT_TIMEOUT;
    bool monitoring = WaitForSingleObject(monitor.get(), 0) == WAIT_TIMEOUT;
    for (HANDLE child : {monitor.get(), blockedChild.get(), allowedChild.get()}) {
        TerminateProcess(child, 0);
        WaitForSingleObject(child, 5000);
    }
    fs::remove(blocked);
    fs::remove(allowed);
    if (!stopped || code != ERROR_ACCESS_DISABLED_BY_POLICY || !allowedRunning || !monitoring) {
        std::fprintf(stderr, "Blocked exit %lu; exception running %d; monitor running %d\n", code,
                     allowedRunning, monitoring);
        return 1;
    }
    std::puts("PASS: monitor terminated the matching desktop fixture and preserved its exception");
    return 0;
}
