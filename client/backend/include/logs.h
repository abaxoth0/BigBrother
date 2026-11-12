#pragma once
#include "hub.h"

namespace client {
class LogsModule final : public CommandModule {
public:
    std::optional<Reply> dispatch(std::string_view command, Arguments args, HANDLE peer) override;
};
}
