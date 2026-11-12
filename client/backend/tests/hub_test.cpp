#include "hub.h"
#include "firewall.h"
#include "logs.h"
#include "ipc_daemon.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>

HANDLE g_ServiceStopEvent = INVALID_HANDLE_VALUE;

class FakeFirewall final : public client::FirewallService {
public:
    std::string calls;
    bool succeeds = true;
    bool start() override { calls += 'S'; return succeeds; }
    bool stop() override { calls += 'X'; return succeeds; }
};

int main() {
    FakeFirewall service;
    client::FirewallModule module(service);
    assert(service.calls.empty());
    assert(!module.dispatch("PING", {}, nullptr));
    std::string_view invalid[] = {"1"};
    assert(!module.dispatch("START_FIREWALL", invalid, nullptr)->ok && service.calls.empty());
    assert(module.dispatch("START_FIREWALL", {}, nullptr)->ok && service.calls == "S");
    assert(module.dispatch("STOP_FIREWALL", {}, nullptr)->ok && service.calls == "SX");
    assert(module.dispatch("RESTART_FIREWALL", {}, nullptr)->ok && service.calls == "SXXS");
    service.succeeds = false;
    assert(!module.dispatch("RESTART_FIREWALL", {}, nullptr)->ok && service.calls == "SXXSX");
    service.calls.clear();
    SetIpv6BlockEnabled(0);
    DaemonSetFiltration(1);
    RefreshFiltrationEffective();
    char status[100];
    DaemonGetStatus(status, sizeof(status));
    assert(service.calls.empty());
    g_ServiceStopEvent = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    assert(!module.control(client::FirewallAction::start) && service.calls.empty());
    assert(!module.control(client::FirewallAction::restart) && service.calls.empty());
    CloseHandle(g_ServiceStopEvent);
    g_ServiceStopEvent = INVALID_HANDLE_VALUE;

    wchar_t executable[MAX_PATH]{};
    assert(GetModuleFileNameW(nullptr, executable, MAX_PATH));
    auto directory = std::filesystem::path(executable).parent_path() / "logs";
    std::filesystem::create_directories(directory);
    auto path = directory / "firewall.binlog";
    { std::ofstream file(path, std::ios::binary); file << std::string(70000, '\xAB'); }
    client::LogsModule logs;
    std::string_view args[] = {"firewall", "0", ""};
    auto first = logs.dispatch("READ_LOG", args, nullptr);
    assert(first->ok && first->values[1] == "65536" && first->values[2] == "70000");
    assert(first->values[3].size() == 131072 && first->values[3].substr(0, 4) == "abab");
    args[1] = first->values[1];
    args[2] = first->values[0];
    auto last = logs.dispatch("READ_LOG", args, nullptr);
    assert(last->ok && last->values[1] == "70000" && last->values[3].size() == (70000 - 65536) * 2);
    { std::ofstream file(path, std::ios::binary | std::ios::trunc); file << "new"; }
    args[1] = "70000";
    auto truncated = logs.dispatch("READ_LOG", args, nullptr);
    assert(truncated->ok && truncated->values[1] == "3" && truncated->values[3] == "6e6577");
    auto old = directory / "previous.binlog";
    std::filesystem::remove(old);
    std::filesystem::rename(path, old);
    { std::ofstream file(path, std::ios::binary); file << "replacement"; }
    args[1] = "3";
    auto rotated = logs.dispatch("READ_LOG", args, nullptr);
    assert(rotated->ok && rotated->values[1] == "11" && rotated->values[0] != first->values[0]);
    args[0] = "../config/config.ini";
    assert(!logs.dispatch("READ_LOG", args, nullptr)->ok);
    args[0] = "client";
    args[1] = "12garbage";
    assert(!logs.dispatch("READ_LOG", args, nullptr)->ok);
    args[1] = "-2";
    assert(!logs.dispatch("READ_LOG", args, nullptr)->ok);
    client::Hub hub;
    assert(!hub.dispatch("UNKNOWN_APP", {}, nullptr));
    assert(!hub.dispatch("INSTALL_CLIENT_UPDATE", {}, nullptr)->ok);
    assert(!hub.dispatch("GET_APPROVED_UPDATE", {}, nullptr)->ok);
    puts("PASS: explicit firewall controls, failed restart, shutdown gate, bounded broker logs, truncation, rotation, and update authorization");
}
