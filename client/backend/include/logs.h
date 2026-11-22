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
    /** @brief Handle READ_LOG, returning identity, next offset, size and hex data; otherwise nullopt. */
    std::optional<Reply> dispatch(std::string_view command, Arguments args, HANDLE peer) override;
};
}
