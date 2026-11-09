#pragma once

#include <windows.h>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace client {

struct Reply {
    bool ok = true;
    std::vector<std::string> values;
    static Reply error(std::string message) { return {false, {std::move(message)}}; }
};

using Arguments = std::span<const std::string_view>;

class CommandModule {
public:
    virtual ~CommandModule() = default;
    virtual std::optional<Reply> dispatch(std::string_view command, Arguments args, HANDLE peer) = 0;
};

class Hub {
    std::vector<std::unique_ptr<CommandModule>> modules_;
public:
    Hub();
    void add(std::unique_ptr<CommandModule> module);
    std::optional<Reply> dispatch(std::string_view command, Arguments args, HANDLE peer);
};

Hub& hub();

}
