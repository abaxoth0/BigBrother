/**
 * @file process_monitors.h
 * @brief Desktop monitor supervision for one authenticated server subscription.
 */
#pragma once
#include "resource.h"
#include <map>

#include <string>

namespace client {
/** @brief Owns a separate kill-on-close job for each active desktop session. */
class ProcessMonitors {
    std::wstring rules_;
    /** @brief Process and owning job for a single user session. */
    struct Worker {
        Handle process;
        Handle job;
    };
    std::map<DWORD, Worker> children_;

public:
    /** @brief Replace the policy, stopping workers with superseded rules. Reject invalid UTF-8 or quoting. */
    bool replace(const char* rules);
    /** @brief Launch missing workers for active sessions. Called only from the subscription thread. */
    void refresh();
};
}
