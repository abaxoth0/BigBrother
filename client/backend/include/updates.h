/**
 * @file updates.h
 * @brief Supervised update preparation commands.
 */

#pragma once
#include "hub.h"
#include <mutex>
#include "resource.h"

namespace client {
/** @brief Launch a supervised update worker and restrict approved metadata to that process. */
class UpdatesModule final : public CommandModule {
    std::mutex mutex_;
    std::unique_ptr<Handle> worker_;
    Handle job_{CreateJobObjectW(nullptr, nullptr)};
    std::string approved_;

public:
    /** @brief Handle update preparation/metadata requests, or return nullopt for other commands. */
    std::optional<Reply> dispatch(std::string_view command, Arguments args, HANDLE peer) override;
};
}
