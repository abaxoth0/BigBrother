/**
 * @file firewall.h
 * @brief Firewall command adapter and service lifecycle control.
 */

#pragma once

#include "hub.h"
#include <mutex>

namespace client {

/** @brief Requested service transition; restart stops before starting again. */
enum class FirewallAction { start, stop, restart };

/** @brief Service-control interface; start/stop report whether the requested state was reached. */
class FirewallService {
public:
    /** @brief Release an implementation through the service-control interface. */
    virtual ~FirewallService() = default;
    /** @brief Ensure the service is ready; return false if startup or readiness checks fail. */
    virtual bool start() = 0;
    /** @brief Ensure the service is stopped; return false if the stop cannot be completed. */
    virtual bool stop() = 0;
};

/** @brief Firewall commands and serialized service lifecycle operations. */
class FirewallModule final : public CommandModule {
    FirewallService& service_;
    std::mutex control_;

public:
    /** @brief Borrow a service-control implementation that must outlive this module. */
    explicit FirewallModule(FirewallService& service) : service_(service) {}
    /**
     * @brief Start, stop or restart the service; reject starts during backend shutdown.
     * @return true after successful control and state notification, false on failure.
     */
    bool control(FirewallAction action);
    /** @brief Handle whitelist, filtration and service commands; return nullopt for other commands. */
    std::optional<Reply> dispatch(std::string_view command, Arguments args, HANDLE peer) override;
    /**
     * @brief Exchange one message with the firewall pipe using bounded I/O waits.
     * Arguments are concatenated after the command line. The response is NUL-terminated
     * and stripped of trailing CR/LF on success. No handles are retained.
     * @return 0 on success, -1 for invalid input, transport failure or an undersized output buffer.
     */
    static int request(const char* command, const char** args, size_t count, char* output, size_t capacity);
};

/** @brief Return the shared module backed by Windows service control. */
FirewallModule& firewall();

}
