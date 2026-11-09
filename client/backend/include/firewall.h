#pragma once

#include "hub.h"
#include <mutex>

namespace client {

enum class FirewallAction { start, stop, restart };

class FirewallService {
public:
    virtual ~FirewallService() = default;
    virtual bool start() = 0;
    virtual bool stop() = 0;
};

class FirewallModule final : public CommandModule {
    FirewallService& service_;
    std::mutex control_;
public:
    explicit FirewallModule(FirewallService& service) : service_(service) {}
    bool control(FirewallAction action);
    std::optional<Reply> dispatch(std::string_view command, Arguments args, HANDLE peer) override;
    static int request(const char* command, const char** args, size_t count, char* output, size_t capacity);
};

FirewallModule& firewall();

}
