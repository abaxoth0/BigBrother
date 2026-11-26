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

/** @brief Coordinate local preference changes with the subscription that supervises the monitor jobs. */
class ProcessMonitorControl {
    std::mutex mutex_;
    bool enabled_;
    std::uint64_t revision_ = 0;
    std::optional<ProcessMonitors> workers_;

public:
    /** @brief Read the saved device preference. Existing installations default to enabled. */
    ProcessMonitorControl();

    /** @brief Persist the preference and release current jobs before acknowledging a change. */
    bool set_enabled(bool enabled);

    /** @brief Return the current policy request revision, or no revision while locally disabled. */
    std::optional<std::uint64_t> enabled_revision();

    /** @brief Apply an authenticated response only while its local enable revision is current. */
    bool publish(std::uint64_t revision, std::string_view payload);

    /** @brief Start missing workers for the last accepted policy. */
    void refresh();

    /** @brief Drop the subscription's mapping, jobs and outstanding request revision. */
    void disconnect();
};

/** @brief Return the supervisor shared by the subscription thread and local IPC handlers. */
ProcessMonitorControl& process_monitor_control();

}
