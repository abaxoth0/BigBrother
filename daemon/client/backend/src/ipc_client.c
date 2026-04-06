/**
 * @file ipc_client.c
 * @brief Named pipe server for Client Frontend & Backend communication.
 */

#include "../include/ipc_client.h"
#include "../include/ipc_daemon.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include "../../../common/log/log.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

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

static int get_local_ip(char* ip_buf, size_t buf_size) {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        strncpy(ip_buf, "unknown", buf_size - 1);
        return -1;
    }

    struct hostent* he = gethostbyname(hostname);
    if (!he) {
        strncpy(ip_buf, "unknown", buf_size - 1);
        return -1;
    }

    struct in_addr** addr_list = (struct in_addr**)he->h_addr_list;
    if (addr_list[0] == NULL) {
        strncpy(ip_buf, "unknown", buf_size - 1);
        return -1;
    }

    strncpy(ip_buf, inet_ntoa(*addr_list[0]), buf_size - 1);
    return 0;
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
        char ip[64];
        char hostname[256];
        DWORD client_pid = GetCurrentProcessId();

        get_local_ip(ip, sizeof(ip));
        gethostname(hostname, sizeof(hostname));

        // Ping daemon to check status
        int daemon_ok = (PingDaemon() == 0);
        LOGF("[IPC] PingDaemon result: %d (0=success)", daemon_ok);

        char response[512];
        snprintf(response, sizeof(response), "STATUS:%s:%s:%s:%lu\n",
                 hostname,
                 ip,
                 daemon_ok ? "RUNNING" : "NOT_RUNNING",
                 client_pid);
        LOGF("[IPC] GET_STATUS: daemon_ok=%d, response: %s", daemon_ok, response);
        write_response(pipe, response);

    } else if (strcmp(buffer, "GET_WHITELIST") == 0) {
        write_response(pipe, "WHITELIST\n");

        char response[8192];
        if (DaemonGetWhitelist(response, sizeof(response)) == 0) {
            write_response(pipe, response);
        }

    } else if (strcmp(buffer, "RELOAD_WHITELIST") == 0) {
        DaemonReloadWhitelist(buffer, sizeof(buffer));
        write_response(pipe, buffer);

    } else if (strcmp(buffer, "RESTART_CLIENT") == 0) {
        write_ok(pipe);
        CloseHandle(pipe);
        // Exit the process - daemon will restart us
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

        // Debug: log to stderr
        fprintf(stderr, "[IPC] GET_LOG_PATH: client=%s, firewall=%s\n", client_log, firewall_log);

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
            1,
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
