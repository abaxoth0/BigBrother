/**
 * @file process_monitors.cpp
 * @brief Launch client process monitors with desktop-user tokens in a backend-owned Windows job.
 */
#define _WIN32_WINNT 0x0A00
#include "process_monitors.h"
#include "process_policy.h"
#include "log/log.h"
#include <userenv.h>
#include <wtsapi32.h>
#include <vector>

namespace client {

/** @brief Start with the supplied user token and atomically bind the child to the subscription job. */
static Handle launch(HANDLE token, HANDLE job, const std::wstring& rules) {
    std::wstring path(32768, 0);
    DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length == path.size()) {
        return Handle();
    }
    path.resize(length);
    const auto slash = path.find_last_of(L'\\');
    if (slash == path.npos) {
        return Handle();
    }
    const auto directory = path.substr(0, slash + 1);
    path = directory + L"BigBrother Process Monitor.exe";
    std::wstring command = L"\"" + path + L"\" --rules \"" + rules + L"\"";

    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<BYTE> storage(bytes);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    wchar_t desktop[] = L"winsta0\\default";
    startup.StartupInfo.lpDesktop = desktop;
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes)) {
        return Handle();
    }
    PROCESS_INFORMATION process{};
    void* environment = nullptr;
    if (UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &job,
                                  sizeof(job), nullptr, nullptr) &&
        CreateEnvironmentBlock(&environment, token, FALSE)) {
        CreateProcessAsUserW(token, path.c_str(), command.data(), nullptr, nullptr, FALSE,
                             EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                             environment, directory.c_str(), &startup.StartupInfo, &process);
        DestroyEnvironmentBlock(environment);
    }
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    Handle thread(process.hThread);
    return Handle(process.hProcess);
}

/** @brief Prefer the user's filtered desktop token when the session supplied an elevated token. */
static Handle start_session(DWORD session, HANDLE job, const std::wstring& rules) {
    HANDLE raw = nullptr;
    if (!WTSQueryUserToken(session, &raw)) {
        return Handle();
    }
    Handle token(raw);
    TOKEN_ELEVATION elevation{};
    DWORD bytes = 0;
    if (!GetTokenInformation(raw, TokenElevation, &elevation, sizeof(elevation), &bytes)) {
        return Handle();
    }
    if (!elevation.TokenIsElevated) {
        return launch(raw, job, rules);
    }
    TOKEN_LINKED_TOKEN linked{};
    if (!GetTokenInformation(raw, TokenLinkedToken, &linked, sizeof(linked), &bytes)) {
        return Handle();
    }
    Handle limited(linked.LinkedToken);
    HANDLE primary = nullptr;
    if (!DuplicateTokenEx(limited.get(), MAXIMUM_ALLOWED, nullptr, SecurityImpersonation, TokenPrimary,
                          &primary)) {
        return Handle();
    }
    Handle desktop(primary);
    return launch(primary, job, rules);
}

bool ProcessMonitors::replace(const char* rules) {
    if (strlen(rules) > 8192) {
        return false;
    }
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, rules, -1, nullptr, 0);
    if (!size) {
        return false;
    }
    std::wstring next(size, 0);
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, rules, -1, next.data(), size)) {
        return false;
    }
    next.pop_back();
    if (next != rules_) {
        std::vector<process_policy::Rule> parsed;
        if (!process_policy::parse_list(next, parsed)) {
            return false;
        }
        children_.clear();
        rules_ = std::move(next);
    }
    return true;
}

void ProcessMonitors::refresh() {
    if (rules_.empty()) {
        return;
    }
    std::erase_if(children_, [](const auto& child) {
        return WaitForSingleObject(child.second.process.get(), 0) != WAIT_TIMEOUT;
    });
    WTS_SESSION_INFOW* sessions = nullptr;
    DWORD count = 0;
    if (!WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessions, &count)) {
        return;
    }
    for (DWORD i = 0; i < count; ++i) {
        DWORD id = sessions[i].SessionId;
        if (!id || sessions[i].State != WTSActive || children_.contains(id)) {
            continue;
        }
        Handle job(CreateJobObjectW(nullptr, nullptr));
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job ||
            !SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            LOGF("[Process Monitor] Could not configure supervisor job: %lu", GetLastError());
            continue;
        }
        auto process = start_session(id, job.get(), rules_);
        if (process) {
            children_.emplace(id, Worker{std::move(process), std::move(job)});
        } else {
            LOGF("[Process Monitor] Could not start monitor for session %lu", id);
        }
    }
    WTSFreeMemory(sessions);
}
}
