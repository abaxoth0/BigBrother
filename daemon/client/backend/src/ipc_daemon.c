/**
 * @file ipc_daemon.c
 * @brief IPC client implementation for connecting to BigBrother Daemon.
 */

#include "../include/ipc_daemon.h"
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define DAEMON_PIPE_PREFIX "\\\\.\\pipe\\" DAEMON_PIPE_NAME

static char g_server_ip[64] = {0};

void SetServerIp(const char* ip) {
    if (ip) {
        strncpy(g_server_ip, ip, sizeof(g_server_ip) - 1);
    }
}

static int send_command(const char* command, const char* data, size_t data_size,
                       char* out_buffer, size_t buffer_size) {
    if (!command || !out_buffer || buffer_size == 0) {
        return -1;
    }

    HANDLE pipe = CreateFile(
        DAEMON_PIPE_PREFIX,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (pipe == INVALID_HANDLE_VALUE) {
        return -1;
    }

    DWORD written;
    size_t cmd_len = strlen(command);
    char* send_buf = malloc(cmd_len + 1 + data_size + 1);
    if (!send_buf) {
        CloseHandle(pipe);
        return -1;
    }

    memcpy(send_buf, command, cmd_len);
    send_buf[cmd_len] = '\n';
    if (data && data_size > 0) {
        memcpy(send_buf + cmd_len + 1, data, data_size);
    }
    size_t total_len = cmd_len + 1 + (data && data_size > 0 ? data_size : 0);

    WriteFile(pipe, send_buf, (DWORD)total_len, &written, NULL);
    free(send_buf);

    FlushFileBuffers(pipe);

    DWORD bytes_read = 0;
    if (!ReadFile(pipe, out_buffer, (DWORD)(buffer_size - 1), &bytes_read, NULL)) {
        CloseHandle(pipe);
        return -1;
    }

    out_buffer[bytes_read] = '\0';

    while (bytes_read > 0 && (out_buffer[bytes_read - 1] == '\n' || out_buffer[bytes_read - 1] == '\r')) {
        out_buffer[--bytes_read] = '\0';
    }

    CloseHandle(pipe);
    return 0;
}

int DaemonGetStatus(char* out_buffer, size_t buffer_size) {
    return send_command("GET_STATUS", NULL, 0, out_buffer, buffer_size);
}

int DaemonGetWhitelist(char* out_buffer, size_t buffer_size) {
    return send_command("GET_WHITELIST", NULL, 0, out_buffer, buffer_size);
}

int DaemonSetWhitelist(const char* data, size_t size, char* out_buffer, size_t buffer_size) {
    return send_command("SET_WHITELIST", data, size, out_buffer, buffer_size);
}

int DaemonReloadWhitelist(char* out_buffer, size_t buffer_size) {
    return send_command("RELOAD", NULL, 0, out_buffer, buffer_size);
}

static int connect_to_server(const char* server_ip, char* out_buffer, size_t buffer_size) {
    if (!server_ip || !out_buffer || buffer_size == 0) {
        return -1;
    }

    char pipe_path[128];
    snprintf(pipe_path, sizeof(pipe_path), "\\\\%s\\pipe\\BigBrother", server_ip);

    HANDLE pipe = CreateFile(
        pipe_path,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (pipe == INVALID_HANDLE_VALUE) {
        return -1;
    }

    const char* cmd = "GET_WHITELIST\n";
    DWORD written;
    WriteFile(pipe, cmd, (DWORD)strlen(cmd), &written, NULL);
    FlushFileBuffers(pipe);

    DWORD bytes_read = 0;
    if (!ReadFile(pipe, out_buffer, (DWORD)(buffer_size - 1), &bytes_read, NULL)) {
        CloseHandle(pipe);
        return -1;
    }

    out_buffer[bytes_read] = '\0';

    while (bytes_read > 0 && (out_buffer[bytes_read - 1] == '\n' || out_buffer[bytes_read - 1] == '\r')) {
        out_buffer[--bytes_read] = '\0';
    }

    CloseHandle(pipe);
    return 0;
}

int PingDaemon(void) {
    char buffer[256];
    return send_command("PING", NULL, 0, buffer, sizeof(buffer));
}

int DaemonRun(const char* server_ip, int poll_interval_secs) {
    char whitelist_buf[8192];
    char last_whitelist[8192] = {0};
    int connected = 0;

    while (1) {
        if (PingDaemon() != 0) {
            char buf[256];
            snprintf(buf, sizeof(buf), "[Daemon] Local daemon not responding, exiting (pid: %lu)\n", GetCurrentProcessId());
            OutputDebugString(buf);
            break;
        }

        if (connected) goto wait;

        if (connect_to_server(server_ip, whitelist_buf, sizeof(whitelist_buf)) == 0) {
            connected = 1;
            if (strcmp(whitelist_buf, last_whitelist) == 0) goto wait;

            strncpy(last_whitelist, whitelist_buf, sizeof(last_whitelist) - 1);
            if (DaemonSetWhitelist(whitelist_buf, strlen(whitelist_buf), whitelist_buf, sizeof(whitelist_buf)) != 0) {
                OutputDebugString("[Daemon] Failed to set whitelist on daemon\n");
            } else {
                OutputDebugString("[Daemon] Whitelist updated\n");
            }
        } else {
            char buf[256];
            snprintf(buf, sizeof(buf), "[Daemon] Cannot connect to server %s, retrying...\n", server_ip);
            OutputDebugString(buf);
            connected = 0;
        }
    wait:
        Sleep(poll_interval_secs * 1000);
    }

    return 0;
}
