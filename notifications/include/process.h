/**
 * @file process.h
 * @brief Supervised notification child processes.
 */

#pragma once

#include "app.h"

namespace notifications {

/** @brief Own a job that terminates its supervised processes when the group is destroyed. */
class process_group {
    handle job_{CreateJobObjectW(nullptr, nullptr)};

public:
    process_group() {
        check_win32(job_.get() != nullptr, L"Create notification process job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_BREAKAWAY_OK;
        check_win32(
            SetInformationJobObject(job_.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)),
            L"Configure notification process job");
    }
    /**
     * @brief Take ownership of a suspended process/thread, assign it to the job and resume it.
     * @return Owned process handle; the thread handle is closed.
     * @throws failure If assignment/resume fails; the child is terminated before throwing.
     */
    handle resume(PROCESS_INFORMATION process) {
        handle child(process.hProcess), thread(process.hThread);
        if (!AssignProcessToJobObject(job_.get(), child.get())) {
            DWORD error = GetLastError();
            TerminateProcess(child.get(), error);
            throw failure{HRESULT_FROM_WIN32(error), L"Supervise notification process"};
        }
        if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
            DWORD error = GetLastError();
            TerminateProcess(child.get(), error);
            throw failure{HRESULT_FROM_WIN32(error), L"Resume notification process"};
        }
        return child;
    }
};

}
