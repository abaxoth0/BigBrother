#pragma once
#include "hub.h"
#include <mutex>
#include "resource.h"

namespace client {
class UpdatesModule final : public CommandModule {
    std::mutex mutex_;
    std::unique_ptr<Handle> worker_;
    Handle job_{CreateJobObjectW(nullptr, nullptr)};
    std::string approved_;
public:
    std::optional<Reply> dispatch(std::string_view command, Arguments args, HANDLE peer) override;
};
}
