/**
 * @file ipc_client.c
 * @brief Named pipe server for Client Frontend & Backend communication.
 */

#include "../include/ipc_client.h"
#include "../include/ipc_daemon.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CLIENT_PIPE_NAME "\\\\.\\pipe\\BigBrother Client"
#define CLIENT_PIPE_BUFFER_SIZE 4096

static void write_response(HANDLE pipe, const char* response) {
    DWORD written;
    WriteFile(pipe, response, (DWORD)strlen(response), &written, NULL);
    FlushFileBuffers(pipe);
}

static void write_ok(HANDLE pipe) {
    write_response(pipe, "OK\n");
}

static void write_error(HANDLE pipe, const char* error) {
    char buf[512];
    snprintf(buf, sizeof(buf), "ERROR:%s\n", error);
    write_response(pipe, buf);
}

static DWORD WINAPI client_handler(LPVOID param) {
    HANDLE pipe = (HANDLE)param;
    char buffer[CLIENT_PIPE_BUFFER_SIZE];
    DWORD bytes_read;

    if (!ReadFile(pipe, buffer, sizeof(buffer) - 1, &bytes_read, NULL)) {
        CloseHandle(pipe);
        return 1;
    }

    buffer[bytes_read] = '\0';

    // Remove trailing newlines
    while (bytes_read > 0 && (buffer[bytes_read - 1] == '\n' || buffer[bytes_read - 1] == '\r')) {
        buffer[--bytes_read] = '\0';
    }

    // TODO: Refactor communication protocol to use ints for commands instead of human readable strings

    // Parse command
    if (strcmp(buffer, "GET_STATUS") == 0) {
        int daemon_ok = (PingDaemon() == 0);

        char response[512];
        snprintf(response, sizeof(response), "STATUS\n0\n0\n%u\n%s\n",
                 g_whitelist_revision,
                 daemon_ok ? "running" : "not_running");
        write_response(pipe, response);

    } else if (strcmp(buffer, "GET_WHITELIST") == 0) {
        char response[8192];
        if (DaemonGetWhitelist(response, sizeof(response)) == 0) {
            write_response(pipe, response);
        } else {
            write_response(pipe, "WHITELIST\n");
        }

    } else if (strcmp(buffer, "RELOAD_WHITELIST") == 0) {
        DaemonReloadWhitelist(buffer, sizeof(buffer));
        write_response(pipe, buffer);

    } else if (strcmp(buffer, "RESTART_CLIENT") == 0) {
        write_ok(pipe);
        CloseHandle(pipe);
        ExitProcess(0);

    } else if (strcmp(buffer, "PING") == 0) {
        write_ok(pipe);

    } else if (strcmp(buffer, "GET_LOG_PATH") == 0) {
        char exe_path[MAX_PATH];
        char* name = exe_path + GetModuleFileName(NULL, exe_path, sizeof(exe_path));
        while (name > exe_path && *(name - 1) != '\\') name--;
        *name = '\0';

        char client_log[512];
        snprintf(client_log, sizeof(client_log), "%s\\logs\\client.binlog", exe_path);

        char firewall_log[512];
        snprintf(firewall_log, sizeof(firewall_log), "%s\\logs\\firewall.binlog", exe_path);

        char response[1200];
        snprintf(response, sizeof(response), "LOG_PATH:%s|%s\n", client_log, firewall_log);
        write_response(pipe, response);

    } else {
        write_error(pipe, "unknown command");
    }

    CloseHandle(pipe);
    return 0;
}

DWORD WINAPI client_server_thread(LPVOID param) {
    (void)param;

    while (1) {
        HANDLE pipe = CreateNamedPipe(
            CLIENT_PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            CLIENT_PIPE_BUFFER_SIZE,
            CLIENT_PIPE_BUFFER_SIZE,
            0,
            NULL
        );

        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(1000);
            continue;
        }

        if (ConnectNamedPipe(pipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
            HANDLE thread = CreateThread(NULL, 0, client_handler, pipe, 0, NULL);
            if (thread) {
                CloseHandle(thread);
            }
        } else {
            CloseHandle(pipe);
        }
    }

    return 0;
}

void StartClientServer(void) {
    HANDLE thread = CreateThread(NULL, 0, client_server_thread, NULL, 0, NULL);
    if (thread) {
        CloseHandle(thread);
    }
}
