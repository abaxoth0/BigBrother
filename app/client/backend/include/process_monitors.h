/**
 * @file process_monitors.h
 * @brief Desktop monitor supervision for one authenticated server subscription.
 */

#pragma once

#include "resource.h"
#include "policy_snapshot.h"
#include <map>
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

}
