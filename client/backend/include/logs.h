/**
 * @file logs.h
 * @brief Log streaming commands for the client hub.
 */

#pragma once
#include "hub.h"

namespace client {
/** @brief Read bounded log chunks with file identity and offset tracking across rotation. */
class LogsModule final : public CommandModule {
public:
    /**
     * @brief Handle READ_LOG with source (client/firewall), decimal offset and previous file identity.
     * An offset of -1 begins within the last 512 KiB; rotation or truncation resets a stale offset.
     * @return Identity, next offset, file size and up to 64 KiB as hex; a missing file yields
     * empty identity/data and zero offsets. Invalid arguments or I/O failures produce an error;
     * unrelated commands return std::nullopt. The peer handle is unused.
     */
    std::optional<Reply> dispatch(std::string_view command, Arguments args, HANDLE peer) override;
};
}
