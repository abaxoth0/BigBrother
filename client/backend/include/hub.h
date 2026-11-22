/**
 * @file hub.h
 * @brief Backend command routing and replies.
 */

#pragma once

#include <windows.h>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace client {

/** @brief Command result; values contain response fields or an error message when ok is false. */
struct Reply {
    bool ok = true;
    std::vector<std::string> values;
    /** @brief Build a failed reply owning one error-message field. */
    static Reply error(std::string message) {
        return {false, {std::move(message)}};
    }
};

/** @brief Borrowed argument views valid only for the duration of dispatch. */
using Arguments = std::span<const std::string_view>;

/** @brief Backend command handler. Implementations must tolerate concurrent IPC callers. */
class CommandModule {
public:
    /** @brief Destroy a module through the hub's owning base pointer. */
    virtual ~CommandModule() = default;
    /**
     * @brief Handle a command, or return std::nullopt to let the next module try.
     * @param command Command name without framing.
     * @param args Borrowed arguments; copy any data retained after dispatch.
     * @param peer Borrowed IPC pipe handle, used when caller identity is required.
     */
    virtual std::optional<Reply> dispatch(std::string_view command, Arguments args, HANDLE peer) = 0;
};

/** @brief Dispatch commands to registered modules in insertion order. */
class Hub {
    std::vector<std::unique_ptr<CommandModule>> modules_;

public:
    /** @brief Install the firewall, log and update command modules. */
    Hub();
    /** @brief Take ownership of a module; call before concurrent dispatch begins. */
    void add(std::unique_ptr<CommandModule> module);
    /** @brief Return the first module reply, or std::nullopt for an unknown command. */
    std::optional<Reply> dispatch(std::string_view command, Arguments args, HANDLE peer);
};

/** @brief Return the process-wide hub, initialized on first use. */
Hub& hub();

}
