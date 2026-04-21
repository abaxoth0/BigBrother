/**
 * @file main.c
 * @brief Entry point.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "../include/ipc_daemon.h"
#include "../include/ipc_client.h"
#include "../../../common/log/log.h"

static void print_usage(const char* prog) {
    printf("Usage: %s <command>\n", prog);
    printf("       %s -d <server-ip> [poll-interval]\n", prog);
    printf("\nLocal Commands:\n");
    printf("  status        - Get daemon status\n");
    printf("  whitelist     - Get current whitelist\n");
    printf("  reload        - Reload whitelist from file\n");
    printf("  set <file>    - Set whitelist from file\n");
    printf("\nServer Commands:\n");
    printf("  server <ip>       - Set server IP address\n");
    printf("  register <name> - Register user on server (pending approval)\n");
    printf("  connect           - Connect to server using saved username\n");
    printf("  disconnect        - Disconnect from server\n");
    printf("  refresh           - Refresh server connection\n");
    printf("  change-name <n>   - Change username on server\n");
    printf("  whoami            - Show saved username\n");
    printf("\nDaemon mode:\n");
    printf("  -d <server-ip>        - Run as daemon, connect to server\n");
    printf("  [poll-interval]      - Polling interval in seconds (default: 5)\n");
}

static void print_response(const char* cmd, int result, const char* response) {
    if (result != 0) {
        printf("[%s] Error: could not connect to daemon\n", cmd);
        return;
    }

    printf("[%s] Response:\n%s\n", cmd, response);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    log_init();
    printf("[DEBUG] main: log_init done\n");

    // Load server IP from config
    printf("[DEBUG] main: calling load_server_ip\n");
    load_server_ip();
    printf("[DEBUG] main: load_server_ip done\n");

    // Check for daemon mode
    if (strcmp(argv[1], "-d") == 0) {
        if (argc < 3) {
            printf("Error: specify server IP\n");
            return 1;
        }

        const char* server_ip = argv[2];
        int poll_interval = 5;

        if (argc >= 4) {
            poll_interval = atoi(argv[3]);
            if (poll_interval <= 0) poll_interval = 5;
        }

        SetServerIp(server_ip);

        LOGF("[Main] Starting daemon mode, server: %s, poll interval: %ds", server_ip, poll_interval);
        LOGF("[Main] Starting client server...");
        StartClientServer();

        return DaemonRun(server_ip, poll_interval);
    }

    char buffer[4096];

    if (strcmp(argv[1], "status") == 0) {
        int result = DaemonGetStatus(buffer, sizeof(buffer));
        print_response("status", result, buffer);

    } else if (strcmp(argv[1], "whitelist") == 0) {
        int result = DaemonGetWhitelist(buffer, sizeof(buffer));
        print_response("whitelist", result, buffer);

    } else if (strcmp(argv[1], "reload") == 0) {
        int result = DaemonReloadWhitelist(buffer, sizeof(buffer));
        print_response("reload", result, buffer);

    } else if (strcmp(argv[1], "set") == 0) {
        if (argc < 3) {
            printf("Error: specify file\n");
            return 1;
        }

        FILE* f = fopen(argv[2], "rb");
        if (!f) {
            printf("Error: could not open file '%s'\n", argv[2]);
            return 1;
        }

        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        char* data = malloc(size + 1);
        fread(data, 1, size, f);
        data[size] = '\0';
        fclose(f);

        int result = DaemonSetWhitelist(data, size, buffer, sizeof(buffer));
        print_response("set", result, buffer);
        free(data);

    } else if (strcmp(argv[1], "server") == 0) {
        if (argc < 3) {
            printf("Error: specify server IP\n");
            return 1;
        }
        SetServerIp(argv[2]);
        printf("[server] Server IP set to: %s\n", argv[2]);

    } else if (strcmp(argv[1], "register") == 0) {
        if (argc < 3) {
            printf("Error: specify username\n");
            return 1;
        }
        if (ServerRegister(argv[2]) != 0) {
            printf("[register] Failed to register\n");
            return 1;
        }
        SaveUserName(argv[2]);
        printf("[register] Registered as '%s' (pending approval)\n", argv[2]);

    } else if (strcmp(argv[1], "connect") == 0) {
        char username[128];
        if (LoadUserName(username, sizeof(username)) != 0 || username[0] == '\0') {
            printf("[connect] No saved username. Use 'register <name>' first.\n");
            return 1;
        }
        if (ServerConnect(username) != 0) {
            printf("[connect] Failed (user may not be registered/approved)\n");
            return 1;
        }
        printf("[connect] Connected as '%s'\n", username);

    } else if (strcmp(argv[1], "disconnect") == 0) {
        char username[128];
        if (LoadUserName(username, sizeof(username)) != 0 || username[0] == '\0') {
            printf("[disconnect] No saved username\n");
            return 1;
        }
        if (ServerDisconnect(username) != 0) {
            printf("[disconnect] Failed\n");
            return 1;
        }
        printf("[disconnect] Disconnected\n");

    } else if (strcmp(argv[1], "refresh") == 0) {
        char username[128];
        if (LoadUserName(username, sizeof(username)) != 0 || username[0] == '\0') {
            printf("[refresh] Not connected (no saved username)\n");
            return 1;
        }
        if (ServerRefresh(username) != 0) {
            printf("[refresh] Failed\n");
            return 1;
        }
        printf("[refresh] Connection refreshed\n");

    } else if (strcmp(argv[1], "change-name") == 0) {
        if (argc < 3) {
            printf("Error: specify new username\n");
            return 1;
        }
        char old_name[128];
        if (LoadUserName(old_name, sizeof(old_name)) != 0 || old_name[0] == '\0') {
            printf("[change-name] No saved username\n");
            return 1;
        }
        if (ServerChangeName(old_name, argv[2]) != 0) {
            printf("[change-name] Failed\n");
            return 1;
        }
        SaveUserName(argv[2]);
        printf("[change-name] Changed from '%s' to '%s'\n", old_name, argv[2]);

    } else if (strcmp(argv[1], "whoami") == 0) {
        char username[128];
        if (LoadUserName(username, sizeof(username)) != 0 || username[0] == '\0') {
            printf("[whoami] No saved username\n");
            return 1;
        }
        printf("[whoami] %s\n", username);

    } else {
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}
