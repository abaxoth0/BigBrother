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
    /**
     * @brief Serialize update preparation and restrict approved metadata to the launched worker.
     * INSTALL_CLIENT_UPDATE accepts one nonempty metadata field up to 128 KiB and launches
     * the GUI helper under the requester's token. A running worker rejects another install.
     * GET_APPROVED_UPDATE accepts no fields and verifies the pipe peer's process ID.
     * @return Empty success after launch, metadata for the matching worker, an error for
     * validation/launch failures, or std::nullopt for unrelated commands.
     * Borrows peer only for this call; owns the worker process and supervising job afterward.
     */
    std::optional<Reply> dispatch(std::string_view command, Arguments args, HANDLE peer) override;
};
}
