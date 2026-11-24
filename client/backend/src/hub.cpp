/**
 * @file hub.cpp
 * @brief Registration and ordered dispatch of backend command modules.
 */

#include "hub.h"
#include "firewall.h"
#include "logs.h"
#include "updates.h"

namespace client {
namespace {
/** @brief Hub-owned adapter forwarding to the process-wide firewall module. */
class FirewallCommands final : public CommandModule {
public:
    /** @brief Delegate to firewall() without taking ownership of its service or peer handle. */
    std::optional<Reply> dispatch(std::string_view command, Arguments args, HANDLE peer) override {
        return firewall().dispatch(command, args, peer);
    }
};
}

Hub::Hub() {
    add(std::make_unique<FirewallCommands>());
    add(std::make_unique<LogsModule>());
    add(std::make_unique<UpdatesModule>());
}

void Hub::add(std::unique_ptr<CommandModule> module) {
    modules_.push_back(std::move(module));
}

std::optional<Reply> Hub::dispatch(std::string_view command, Arguments args, HANDLE peer) {
    for (auto& module : modules_) {
        if (auto reply = module->dispatch(command, args, peer)) {
            return reply;
        }
    }
    return std::nullopt;
}

Hub& hub() {
    static Hub instance;
    return instance;
}

}
