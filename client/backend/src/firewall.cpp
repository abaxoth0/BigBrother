#include "firewall.h"
#include "resource.h"
#include "ipc_daemon.h"
#include "ipc_client.h"
#include <cstring>
#include <sstream>

namespace client {
namespace {

bool wait_for_service(SC_HANDLE service, DWORD state) {
    const auto deadline = GetTickCount64() + 30000;
    SERVICE_STATUS status{};
    do {
        if (!QueryServiceStatus(service, &status)) return false;
        if (status.dwCurrentState == state) return true;
        Sleep(100);
    } while (GetTickCount64() < deadline);
    SetLastError(ERROR_TIMEOUT);
    return false;
}

class WindowsFirewallService final : public FirewallService {
public:
    bool start() override {
        ServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
        if (!manager) return false;
        ServiceHandle service(OpenServiceW(manager.get(), L"BigBrother Firewall", SERVICE_START | SERVICE_QUERY_STATUS));
        if (!service) return false;
        SERVICE_STATUS status{};
        if (!QueryServiceStatus(service.get(), &status)) return false;
        if (status.dwCurrentState == SERVICE_STOP_PENDING && !wait_for_service(service.get(), SERVICE_STOPPED)) return false;
        if (!StartServiceW(service.get(), 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) return false;
        if (!wait_for_service(service.get(), SERVICE_RUNNING)) return false;
        const auto deadline = GetTickCount64() + 10000;
        do {
            if (PingDaemon() == 0) return true;
            Sleep(100);
        } while (GetTickCount64() < deadline);
        return false;
    }

    bool stop() override {
        ServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
        if (!manager) return false;
        ServiceHandle service(OpenServiceW(manager.get(), L"BigBrother Firewall", SERVICE_STOP | SERVICE_QUERY_STATUS));
        if (!service) return GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST;
        SERVICE_STATUS status{};
        if (!QueryServiceStatus(service.get(), &status)) return false;
        if (status.dwCurrentState == SERVICE_STOPPED) return true;
        if (status.dwCurrentState != SERVICE_STOP_PENDING && !ControlService(service.get(), SERVICE_CONTROL_STOP, &status)) return false;
        return wait_for_service(service.get(), SERVICE_STOPPED);
    }
};

bool transfer(HANDLE pipe, bool writing, void* buffer, DWORD length, DWORD& transferred, bool* more = nullptr) {
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event) return false;
    OVERLAPPED operation{};
    operation.hEvent = event.get();
    BOOL ok = writing ? WriteFile(pipe, buffer, length, &transferred, &operation)
                      : ReadFile(pipe, buffer, length, &transferred, &operation);
    DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    if (!ok && error == ERROR_IO_PENDING) {
        if (WaitForSingleObject(event.get(), 3000) != WAIT_OBJECT_0) {
            CancelIoEx(pipe, &operation);
            GetOverlappedResult(pipe, &operation, &transferred, TRUE);
            SetLastError(ERROR_TIMEOUT);
            return false;
        }
        ok = GetOverlappedResult(pipe, &operation, &transferred, FALSE);
        error = ok ? ERROR_SUCCESS : GetLastError();
    }
    if (more) *more = error == ERROR_MORE_DATA;
    return ok || (!writing && error == ERROR_MORE_DATA);
}

}

FirewallModule& firewall() {
    static WindowsFirewallService service;
    static FirewallModule module(service);
    return module;
}

bool FirewallModule::control(FirewallAction action) {
    std::lock_guard guard(control_);
    if (g_ServiceStopEvent != INVALID_HANDLE_VALUE &&
        WaitForSingleObject(g_ServiceStopEvent, 0) == WAIT_OBJECT_0 && action != FirewallAction::stop) return false;
    if (action != FirewallAction::start && !service_.stop()) return false;
    if (action != FirewallAction::stop && !service_.start()) return false;
    if (action != FirewallAction::stop) ResetFirewallSync();
    NotifyStateChanged();
    return true;
}

std::optional<Reply> FirewallModule::dispatch(std::string_view command, Arguments args, HANDLE) {
    if (command == "SET_WHITELIST") {
        if (args.size() != 1) return Reply::error("expected whitelist text");
        std::string rules(args[0]);
        char response[256]{};
        return DaemonSetWhitelist(rules.c_str(), rules.size(), response, sizeof(response)) == 0
            ? Reply{} : Reply::error("failed to set whitelist");
    }
    if (command == "GET_WHITELIST") {
        if (!args.empty()) return Reply::error("expected no arguments");
        std::vector<char> buffer(DAEMON_MAX_MESSAGE_SIZE);
        if (DaemonGetWhitelist(buffer.data(), buffer.size()) != 0) return Reply::error("failed to get whitelist");
        std::istringstream lines(buffer.data());
        std::string line;
        if (!std::getline(lines, line) || line != "WHITELIST") return Reply::error("invalid whitelist format");
        Reply reply;
        while (std::getline(lines, line)) if (!line.empty()) reply.values.push_back(std::move(line));
        return reply;
    }
    if (command == "RELOAD_WHITELIST") {
        if (!args.empty()) return Reply::error("expected no arguments");
        char response[256]{};
        return DaemonReloadWhitelist(response, sizeof(response)) == 0 ? Reply{} : Reply::error("failed to reload whitelist");
    }
    if (command == "GET_FILTRATION" || command == "GET_IPV6_BLOCK" ||
        command == "GET_FILTRATION_AUTO_DISABLE" || command == "GET_FALLBACK_WHITELIST") {
        if (!args.empty()) return Reply::error("expected no arguments");
        int value = command == "GET_FILTRATION" ? RefreshFiltrationEffective()
            : command == "GET_IPV6_BLOCK" ? IsIpv6BlockEnabled()
            : command == "GET_FILTRATION_AUTO_DISABLE" ? IsFiltrationAutoDisableEnabled() : IsFallbackWhitelistEnabled();
        return Reply{true, {value ? "1" : "0"}};
    }
    if (command == "SET_FILTRATION" || command == "SET_IPV6_BLOCK" ||
        command == "SET_FILTRATION_AUTO_DISABLE" || command == "SET_FALLBACK_WHITELIST") {
        if (args.size() != 1 || (args[0] != "0" && args[0] != "1")) return Reply::error("expected value 0 or 1");
        int value = args[0] == "1";
        if (command == "SET_FILTRATION" && DaemonSetFiltration(value) != 0) return Reply::error("failed to set filtration");
        if (command == "SET_IPV6_BLOCK" && SetIpv6BlockEnabled(value) != 0) return Reply::error("failed to save IPv6 blocking preference");
        if (command == "SET_FILTRATION_AUTO_DISABLE") SetFiltrationAutoDisableEnabled(value);
        if (command == "SET_FALLBACK_WHITELIST") SetFallbackWhitelistEnabled(value);
        return Reply{};
    }
    FirewallAction action;
    if (command == "START_FIREWALL") action = FirewallAction::start;
    else if (command == "STOP_FIREWALL") action = FirewallAction::stop;
    else if (command == "RESTART_FIREWALL") action = FirewallAction::restart;
    else return std::nullopt;
    if (!args.empty()) return Reply::error("expected no arguments");
    if (!control(action)) return Reply::error("firewall service control failed");
    return Reply{};
}

int FirewallModule::request(const char* command, const char** args, size_t count, char* output, size_t capacity) {
    if (!command || !output || !capacity) return -1;
    output[0] = '\0';
    Handle pipe(CreateFileW(L"\\\\.\\pipe\\BigBrother.Firewall", GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
    if (!pipe) return -1;
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe.get(), &mode, nullptr, nullptr)) return -1;
    std::string request(command);
    request += '\n';
    for (size_t i = 0; i < count; ++i) if (args[i]) request += args[i];
    if (request.size() > DAEMON_MAX_MESSAGE_SIZE) return -1;
    DWORD transferred = 0;
    if (!transfer(pipe.get(), true, request.data(), static_cast<DWORD>(request.size()), transferred) || transferred != request.size()) return -1;
    size_t total = 0;
    do {
        if (total == capacity - 1) return -1;
        bool more = false;
        if (!transfer(pipe.get(), false, output + total, static_cast<DWORD>(capacity - total - 1), transferred, &more)) return -1;
        total += transferred;
        if (!more) break;
    } while (transferred);
    while (total && (output[total - 1] == '\n' || output[total - 1] == '\r')) --total;
    output[total] = '\0';
    return 0;
}

}
