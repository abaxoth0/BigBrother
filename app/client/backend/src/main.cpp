/**
 * @file main.cpp
 * @brief Service, foreground daemon, command-line and maintenance entry points.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "../include/ipc_daemon.h"
#include "../include/ipc_client.h"
#include "log/log.h"
#include "version.h"
#include "firewall.h"
#include "resource.h"
#include "runtime.h"
#include "backend_client.h"
#include <fstream>
#include <iterator>

SERVICE_STATUS g_ServiceStatus = {};
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE g_ServiceStopEvent = INVALID_HANDLE_VALUE;

/**
 * @brief Signal shutdown for Ctrl+C, console close or system shutdown.
 * @return TRUE for handled signals, FALSE so Windows can process other control events.
 */
static BOOL WINAPI ConsoleControlHandler(DWORD control) {
    if (control != CTRL_C_EVENT && control != CTRL_BREAK_EVENT && control != CTRL_CLOSE_EVENT &&
        control != CTRL_SHUTDOWN_EVENT && control != CTRL_LOGOFF_EVENT) return FALSE;
    if (g_ServiceStopEvent != INVALID_HANDLE_VALUE) SetEvent(g_ServiceStopEvent);
    SignalClientServerShutdown();
    return TRUE;
}

/** @brief Report STOP_PENDING and signal backend/IPC shutdown for SCM stop or shutdown controls. */
void WINAPI ServiceControlHandler(DWORD control) {
    if (control != SERVICE_CONTROL_STOP && control != SERVICE_CONTROL_SHUTDOWN) return;
    g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwWaitHint = 30000;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    SetEvent(g_ServiceStopEvent);
    SignalClientServerShutdown();
}

/**
 * @brief Own the single-instance guard, logger, stop event and IPC lifetime around DaemonRun().
 * @param server_ip Optional address override persisted before entering the connection loop.
 * @return Daemon result or a Windows error code if startup fails.
 * On normal exit, stops the firewall and joins IPC workers before closing shared resources.
 */
static int run_backend(const char* server_ip) {
    client::Handle instance(CreateMutexW(nullptr, FALSE, L"Global\\BigBrother.Client.Instance"));
    if (!instance || GetLastError() == ERROR_ALREADY_EXISTS) return ERROR_SERVICE_ALREADY_RUNNING;

    log_init();
    client::Handle stop(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!stop) return static_cast<int>(GetLastError());
    g_ServiceStopEvent = stop.get();
    if (StartClientServer() != 0) return ERROR_OPEN_FAILED;

    if (g_StatusHandle) {
        g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
        g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    } else SetConsoleCtrlHandler(ConsoleControlHandler, TRUE);

    load_server_ip();
    if (server_ip && *server_ip) SetServerIp(server_ip);
    int result = DaemonRun(server_ip);

    SetEvent(g_ServiceStopEvent);
    client::firewall().control(client::FirewallAction::stop);
    StopClientServer();
    log_shutdown();
    g_ServiceStopEvent = INVALID_HANDLE_VALUE;
    return result;
}

/** @brief Register SCM controls, accept an optional -d address and report backend start/stop status. */
void WINAPI ServiceMain(DWORD argc, LPTSTR* argv) {
    g_StatusHandle = RegisterServiceCtrlHandlerA("BigBrother Client", ServiceControlHandler);
    if (!g_StatusHandle) return;
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    g_ServiceStatus.dwWaitHint = 10000;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    const char* server_ip = "";
    for (DWORD i = 1; i + 1 < argc; ++i)
        if (strcmp(argv[i], "-d") == 0) { server_ip = argv[i + 1]; break; }
    g_ServiceStatus.dwWin32ExitCode = run_backend(server_ip);
    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    g_ServiceStatus.dwControlsAccepted = 0;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
}

/** @brief Print the client version, accepted commands and foreground daemon invocation to stdout. */
static void print_usage(const char* prog) {
    printf("BigBrother Client Daemon %s\n", BB_VERSION);
    printf("Usage: %s <command>\n", prog);
    printf("       %s -d <server-ip>\n", prog);
    printf("\nLocal Commands:\n");
    printf("  status        - Get daemon status\n");
    printf("  whitelist     - Get current whitelist\n");
    printf("  reload        - Reload whitelist from file\n");
    printf("  set <file>    - Set whitelist from file\n");
    printf("\nServer Commands:\n");
    printf("  register <name> - Register user on server (pending approval)\n");
    printf("  connect           - Connect to server using saved username\n");
    printf("  disconnect        - Disconnect from server\n");
    printf("  refresh           - Refresh server connection\n");
    printf("  change-name <n>   - Change username on server\n");
    printf("  whoami            - Show saved username\n");
    printf("\nDaemon mode:\n");
    printf("  -d <server-ip>        - Run as daemon, connect to server\n");
}

/**
 * @brief Map command-line arguments to one backend request and print its status and fields.
 * @pre argc is at least two and argv contains the program and command names.
 * @return 0 for an OK reply, 1 for invalid arguments, file input failure or a failed request.
 */
static int run_command(int argc, char** argv) {
    std::string command;
    std::vector<std::string_view> args;
    std::string data;
    std::string_view name(argv[1]);
    if (name == "status") command = "GET_STATUS";
    else if (name == "whitelist") command = "GET_WHITELIST";
    else if (name == "reload") command = "RELOAD_WHITELIST";
    else if (name == "connect") command = "CONNECT";
    else if (name == "disconnect") command = "DISCONNECT";
    else if (name == "refresh") command = "REFRESH";
    else if (name == "whoami") command = "GET_USERNAME";
    else if (name == "start-firewall") command = "START_FIREWALL";
    else if (name == "stop-firewall") command = "STOP_FIREWALL";
    else if (name == "restart-firewall") command = "RESTART_FIREWALL";
    else if ((name == "register" || name == "change-name") && argc == 3) {
        command = name == "register" ? "REGISTER" : "CHANGE_NAME";
        args.push_back(argv[2]);
    } else if (name == "set" && argc == 3) {
        std::ifstream file(argv[2], std::ios::binary | std::ios::ate);
        if (!file || file.tellg() < 0 || file.tellg() > DAEMON_MAX_MESSAGE_SIZE) {
            fprintf(stderr, "Cannot read whitelist file or file exceeds size limit.\n");
            return 1;
        }

        data.resize(static_cast<size_t>(file.tellg()));
        file.seekg(0);
        if (!file.read(data.data(), static_cast<std::streamsize>(data.size()))) return 1;

        command = "SET_WHITELIST";
        args.push_back(data);
    } else {
        print_usage(argv[0]);
        return 1;
    }

    auto reply = client::SendBackendCommand(command, args);
    printf("%s\n", reply.ok ? "OK" : "ERROR");
    for (const auto& value : reply.values) printf("%s\n", value.c_str());
    return reply.ok ? 0 : 1;
}

/**
 * @brief Select command forwarding, service execution, foreground mode or a maintenance helper.
 * Secures persisted credentials before starting the backend or completing --secure-storage.
 * @return Zero on success, a nonzero command/helper result or startup error otherwise.
 */
int main(int argc, char** argv) {
    if (argc == 3 && strcmp(argv[1], "--restart-service") == 0) return RestartBackendService(argv[2]);
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("BigBrother Client Daemon %s\n", BB_VERSION);
        return 0;
    }

    bool daemon_mode = argc == 1 || strcmp(argv[1], "-d") == 0;
    bool secure_storage = argc == 2 && strcmp(argv[1], "--secure-storage") == 0;
    if (!daemon_mode && !secure_storage) return run_command(argc, argv);

    if (PrepareClientSecretPermissions() != 0) {
        fprintf(stderr, "Cannot secure client secrets (error %lu).\n", GetLastError());
        return 1;
    }
    if (secure_storage) return 0;

    SERVICE_TABLE_ENTRY service_table[] = {
        {const_cast<char*>("BigBrother Client"), ServiceMain}, {nullptr, nullptr}
    };
    if (argc == 1 && StartServiceCtrlDispatcherA(service_table)) return 0;
    if (argc >= 2) return run_backend(argc >= 3 ? argv[2] : "");

    print_usage(argv[0]);
    return 1;
}
