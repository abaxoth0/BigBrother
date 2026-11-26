// Run beside BigBrother Process Monitor.exe from an interactive Windows session.
#include "windows_handle.h"
#include "policy_snapshot.h"
#include <sddl.h>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>

using bb::Handle;
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
    auto channel = L"Local\\BigBrother.ProcessMonitor.Test." + std::to_wstring(GetCurrentProcessId());
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    assert(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GR;;;IU)",
                                                                SDDL_REVISION_1, &descriptor, nullptr));
    SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
    Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, &security, PAGE_READWRITE, 0,
                                      sizeof(bb::PolicySnapshot), channel.c_str()));
    assert(mapping && GetLastError() != ERROR_ALREADY_EXISTS);
    LocalFree(descriptor);
    bb::PolicyView<bb::PolicySnapshot> writer;
    assert(writer.open(mapping.get(), FILE_MAP_WRITE));
    auto monitor = start(token.get(), self.parent_path() / L"BigBrother Process Monitor.exe",
                         L"--policy-channel " + channel);
    assert(WaitForSingleObject(monitor.get(), 1200) == WAIT_TIMEOUT);
    assert(WaitForSingleObject(blockedChild.get(), 0) == WAIT_TIMEOUT);
    bool updatesOnly = argc == 2 && std::wstring_view(argv[1]) == L"--updates-only";
    auto names = "bb-monitor-test-" + std::to_string(GetCurrentProcessId()) + "-";
    std::string policy = names + "*.exe\n!" + names + "allowed.exe";
    if (updatesOnly) {
        policy += "\n!" + names + "blocked.exe";
    }
    assert(writer.get()->publish(policy));
    bool stopped = WaitForSingleObject(blockedChild.get(), updatesOnly ? 2200 : 5000) == WAIT_OBJECT_0;
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
    bool expected = updatesOnly ? (!stopped && code == STILL_ACTIVE)
                                : (stopped && code == ERROR_ACCESS_DISABLED_BY_POLICY);
    if (!expected || !allowedRunning || !monitoring) {
        std::fprintf(stderr, "Blocked exit %lu; exception running %d; monitor running %d\n", code,
                     allowedRunning, monitoring);
        return 1;
    }
    std::puts(updatesOnly ? "PASS: live desktop monitor and exempt fixtures stayed running across publication"
                          : "PASS: running monitor applied the new blacklist and preserved its exception");
    return 0;
}
