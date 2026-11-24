/**
 * @file main.cpp
 * @brief Enforce server rules on applications belonging to the monitor's interactive user.
 */
#include "process_policy.h"
#include "resource.h"
#include "version.h"
#include <tlhelp32.h>
#include <cstdio>

using client::Handle;

/** @brief Read a process's interactive session and non-elevated user SID from the same token. */
static bool identity(HANDLE process, DWORD& session, std::vector<BYTE>& user) {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &raw)) {
        return false;
    }
    Handle token(raw);
    TOKEN_ELEVATION elevation{};
    DWORD bytes = 0;
    if (!GetTokenInformation(raw, TokenElevation, &elevation, sizeof(elevation), &bytes) ||
        elevation.TokenIsElevated ||
        !GetTokenInformation(raw, TokenSessionId, &session, sizeof(session), &bytes) || !session) {
        return false;
    }
    if (GetTokenInformation(raw, TokenUser, nullptr, 0, &bytes) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return false;
    }
    user.resize(bytes);
    return GetTokenInformation(raw, TokenUser, user.data(), bytes, &bytes);
}

/** @brief Verify the opened process's image, owner, session and critical flag before termination. */
static void enforce(DWORD pid, const std::vector<process_policy::Rule>& rules, DWORD session, PSID user) {
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pid));
    DWORD targetSession = 0;
    std::vector<BYTE> targetUser;
    BOOL critical = TRUE;
    if (!process || !identity(process.get(), targetSession, targetUser) || targetSession != session ||
        !EqualSid(user, reinterpret_cast<TOKEN_USER*>(targetUser.data())->User.Sid) ||
        !IsProcessCritical(process.get(), &critical) || critical) {
        return;
    }

    wchar_t path[32768];
    DWORD size = static_cast<DWORD>(std::size(path));
    if (!QueryFullProcessImageNameW(process.get(), 0, path, &size)) {
        return;
    }
    std::wstring_view image(path, size);
    auto name = image.substr(image.find_last_of(L'\\') + 1);
    if (process_policy::blocked(rules, name)) {
        TerminateProcess(process.get(), ERROR_ACCESS_DISABLED_BY_POLICY);
    }
}

/** @brief Scan once; inaccessible or already-exited processes are skipped. */
static void scan(const std::vector<process_policy::Rule>& rules, DWORD session, PSID user) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!snapshot || !Process32FirstW(snapshot.get(), &entry)) {
        return;
    }
    do {
        if (entry.th32ProcessID != GetCurrentProcessId() && process_policy::blocked(rules, entry.szExeFile)) {
            enforce(entry.th32ProcessID, rules, session, user);
        }
    } while (Process32NextW(snapshot.get(), &entry));
}

/** @brief Run a job-supervised monitor in an ordinary desktop session until the backend closes its job. */
int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring_view(argv[1]) == L"--version") {
        std::puts("BigBrother Process Monitor " BB_VERSION);
        return 0;
    }
    if (argc != 3 || std::wstring_view(argv[1]) != L"--rules") {
        return 1;
    }
    std::vector<process_policy::Rule> rules;
    if (!process_policy::parse_list(argv[2], rules)) {
        return 1;
    }
    DWORD session = 0;
    std::vector<BYTE> user;
    if (rules.empty() || !identity(GetCurrentProcess(), session, user)) {
        return 1;
    }
    for (;;) {
        scan(rules, session, reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid);
        Sleep(1000);
    }
}
