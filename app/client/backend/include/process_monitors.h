/**
 * @file process_monitors.h
 * @brief Desktop monitor supervision for one authenticated server subscription.
 */

#pragma once

#include "resource.h"
#include "policy_snapshot.h"
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace client {

/** @brief Owns a separate kill-on-close job for each active desktop session. */
class ProcessMonitors {
    std::wstring channel_;
    std::optional<Handle> mapping_;
    bb::PolicyView<bb::PolicySnapshot> view_;
    std::string last_payload_;

    /** @brief Create a subscription-scoped mapping readable by interactive users. */
    bool open_channel();

    /** @brief Process and owning job for a single user session. */
    struct Worker {
        Handle process;
        Handle job;
    };
    std::map<DWORD, Worker> children_;

public:
    /** @brief Forward opaque server text to current and future instances in this subscription. */
    bool publish(std::string_view payload);

    /** @brief Launch missing workers for active sessions. Called only from the subscription thread. */
    void refresh();
};

/** @brief Device preference and the latest confirmed server switch and normalized rules for this subscription. */
struct ProcessBlacklistState {
    bool local_enabled = true;
    std::optional<bool> server_enabled;
    std::string rules;
};

/** @brief Coordinate local preference changes with the subscription that supervises the monitor jobs. */
class ProcessMonitorControl {
    std::mutex mutex_;
    ProcessBlacklistState state_;
    std::uint64_t revision_ = 0;
    std::optional<ProcessMonitors> workers_;

public:
    /** @brief Read the saved device preference. Existing installations default to enabled. */
    ProcessMonitorControl();

    /** @brief Persist the preference and release current jobs before acknowledging a change. */
    bool set_enabled(bool enabled);

    /** @brief Copy a consistent snapshot for the client frontend. An unknown server switch has no readable list. */
    ProcessBlacklistState state();

    /** @brief Return the current request revision, including while local enforcement is paused. */
    std::uint64_t revision();

    /** @brief Retain a current authenticated snapshot and forward rules when both switches are enabled. */
    bool publish(std::uint64_t revision, std::optional<bool> server_enabled, std::string_view payload);

    /** @brief Start missing workers for the last accepted policy. */
    void refresh();

    /** @brief Clear the subscription's cached policy, close its jobs and invalidate outstanding requests. */
    void disconnect();
};

/** @brief Return the supervisor shared by the subscription thread and local IPC handlers. */
ProcessMonitorControl& process_monitor_control();

}
