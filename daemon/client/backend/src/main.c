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
    printf("\nCommands:\n");
    printf("  status      - Get daemon status\n");
    printf("  whitelist   - Get current whitelist\n");
    printf("  reload      - Reload whitelist from file\n");
    printf("  set <file> - Set whitelist from file\n");
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

        LOGF("[Main] Starting daemon mode, server: %s, poll interval: %ds\n", server_ip, poll_interval);
        LOGF("[Main] Starting client server...\n");
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

    } else {
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}
