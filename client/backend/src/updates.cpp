#include "updates.h"
#include <cstring>
#include <userenv.h>

namespace client {

std::optional<Reply> UpdatesModule::dispatch(std::string_view command, Arguments args, HANDLE peer) {
    if (command == "GET_APPROVED_UPDATE") {
        std::lock_guard guard(mutex_);
        ULONG pid = 0;
        if (!args.empty() || !worker_ || !GetNamedPipeClientProcessId(peer, &pid) ||
            GetProcessId(worker_->get()) != pid) return Reply::error("no approved update for this worker");
        return Reply{true, {approved_}};
    }
    if (command != "INSTALL_CLIENT_UPDATE") return std::nullopt;
    if (args.size() != 1 || args[0].empty() || args[0].size() > 128 * 1024) return Reply::error("expected approved update metadata");
    std::lock_guard guard(mutex_);
    if (worker_ && WaitForSingleObject(worker_->get(), 0) == WAIT_TIMEOUT) return Reply::error("an update is already being prepared");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_BREAKAWAY_OK;
    if (!job_ || !SetInformationJobObject(job_.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        return Reply::error("could not supervise update worker");
    ULONG pid = 0;
    if (!GetNamedPipeClientProcessId(peer, &pid)) return Reply::error("could not identify update requester");
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    HANDLE raw_token = nullptr;
    if (!process || !OpenProcessToken(process.get(), TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY, &raw_token)) return Reply::error("could not open requester token");
    Handle token(raw_token);
    wchar_t executable[MAX_PATH]{};
    DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
    if (!length || length == MAX_PATH) return Reply::error("could not locate installation");
    std::wstring path(executable);
    path.resize(path.find_last_of(L"\\/") + 1);
    path += L"GUI\\BigBrother Client.exe";
    std::wstring parameters = L"\"" + path + L"\" --prepare-update " + std::to_wstring(GetCurrentProcessId());
    void* environment = nullptr;
    if (!CreateEnvironmentBlock(&environment, token.get(), FALSE)) return Reply::error("could not create worker environment");
    struct Environment {
        void* value;
        ~Environment() { DestroyEnvironmentBlock(value); }
    } environment_guard{environment};
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.lpDesktop = const_cast<wchar_t*>(L"winsta0\\default");
    PROCESS_INFORMATION child{};
    BOOL started = CreateProcessAsUserW(token.get(), path.c_str(), parameters.data(), nullptr, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED, environment, nullptr, &startup, &child);
    if (!started && GetLastError() == ERROR_PRIVILEGE_NOT_HELD)
        started = CreateProcessWithTokenW(token.get(), 0, path.c_str(), parameters.data(), CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
            environment, nullptr, &startup, &child);
    if (!started) return Reply::error("could not launch update worker");
    Handle thread(child.hThread);
    worker_ = std::make_unique<Handle>(child.hProcess);
    if (!AssignProcessToJobObject(job_.get(), child.hProcess)) {
        TerminateProcess(child.hProcess, ERROR_PROCESS_ABORTED);
        return Reply::error("could not supervise update worker");
    }
    approved_ = args[0];
    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        TerminateProcess(child.hProcess, ERROR_PROCESS_ABORTED);
        return Reply::error("could not resume update worker");
    }
    return Reply{};
}

}
