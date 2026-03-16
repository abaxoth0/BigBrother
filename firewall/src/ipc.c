/**
 * @file ipc.c
 * @brief Named pipe IPC server implementation for BigBrother daemon.
 */

#include "../include/ipc.h"
#include "../include/allowlist.h"
#include "../include/firewall.h"
#include "../../common/encoding/encoding.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define IPC_PIPE_PREFIX "\\\\.\\pipe\\" IPC_PIPE_NAME

static int reload_whitelist(void) {
    return LoadWhiteList(NULL);
}

static void write_response(HANDLE pipe, const char* response) {
    DWORD written;
    WriteFile(pipe, response, (DWORD)strlen(response), &written, NULL);
}

static void write_status(HANDLE pipe) {
    char buffer[IPC_BUFFER_SIZE];
    EncodingFormatStatus(buffer, sizeof(buffer), g_Whitelist.count, g_IpAllowlist.count);
    write_response(pipe, buffer);
}

static void write_ok(HANDLE pipe) {
    char buffer[IPC_BUFFER_SIZE];
    EncodingFormatOk(buffer, sizeof(buffer));
    write_response(pipe, buffer);
}

static void write_error(HANDLE pipe, const char* error) {
    char buffer[IPC_BUFFER_SIZE];
    EncodingFormatError(buffer, sizeof(buffer), error);
    write_response(pipe, buffer);
}

static void write_whitelist(HANDLE pipe) {
    char* domains[256];
    for (size_t i = 0; i < g_Whitelist.count && i < 256; i++) {
        domains[i] = g_Whitelist.entries[i].domain;
    }

    char buffer[IPC_BUFFER_SIZE * 4];
    EncodingFormatWhitelist(buffer, sizeof(buffer), (const char**)domains, g_Whitelist.count);
    write_response(pipe, buffer);
}

static int parse_and_execute(HANDLE pipe, char* buffer, size_t size) {
    buffer[size] = '\0';

    char* newline = strchr(buffer, '\n');
    if (newline) *newline = '\0';

    for (char* p = buffer; *p; p++) {
        if (*p == '\r') *p = '\0';
    }

    IpcMessageType msg = EncodingParseMessageType(buffer);

    switch (msg) {
        case MSG_GET_WHITELIST:
            write_whitelist(pipe);
            break;

        case MSG_SET_WHITELIST: {
            char* data = newline ? newline + 1 : buffer + strlen(buffer);
            size_t data_len = size - (data - buffer);

            if (data_len > 0 && IpcSetWhitelist(data, data_len) == 0) {
                write_ok(pipe);
            } else {
                write_error(pipe, "failed to set whitelist");
            }
            break;
        }

        case MSG_RELOAD:
            if (reload_whitelist() == 0) {
                write_ok(pipe);
            } else {
                write_error(pipe, "failed to reload whitelist");
            }
            break;

        case MSG_GET_STATUS:
            write_status(pipe);
            break;

        default:
            write_error(pipe, "unknown command");
            return -1;
    }

    return 0;
}

static DWORD WINAPI ipc_client_handler(LPVOID param) {
    HANDLE pipe = (HANDLE)param;
    char buffer[IPC_BUFFER_SIZE];
    DWORD bytes_read;

    if (ReadFile(pipe, buffer, sizeof(buffer) - 1, &bytes_read, NULL)) {
        parse_and_execute(pipe, buffer, bytes_read);
    }

    FlushFileBuffers(pipe);
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);

    return 0;
}

static DWORD WINAPI ipc_server_thread(LPVOID param) {
    while (1) {
        // Create pipe - blocking mode, waits for client
        HANDLE pipe = CreateNamedPipeA(
            IPC_PIPE_PREFIX,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            IPC_BUFFER_SIZE,
            IPC_BUFFER_SIZE,
            0,
            NULL
        );

        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(100);
            continue;
        }

        // Wait for client - this blocks until a client connects
        BOOL connected = ConnectNamedPipe(pipe, NULL);
        if (!connected) {
            DWORD err = GetLastError();
            if (err != ERROR_PIPE_CONNECTED) {
                CloseHandle(pipe);
                continue;
            }
        }

        // Handle client in separate thread
        HANDLE thread = CreateThread(NULL, 0, ipc_client_handler, pipe, 0, NULL);
        if (thread) {
            CloseHandle(thread);
        }
    }

    return 0;
}

int IpcStart(void) {
    HANDLE thread = CreateThread(NULL, 0, ipc_server_thread, NULL, 0, NULL);
    if (!thread) {
        return -1;
    }
    CloseHandle(thread);
    return 0;
}

int IpcReloadWhitelist(void) {
    return reload_whitelist();
}

int IpcSetWhitelist(const char* data, size_t size) {
    if (!data || size == 0) {
        return -1;
    }

    WhitelistLoadFromData(&g_Whitelist, data, size);
    IpAllowlistClear(&g_IpAllowlist);

    return 0;
}
