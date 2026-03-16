/**
 * @file ipc.c
 * @brief IPC client implementation for connecting to BigBrother Daemon.
 */

#include "../include/ipc.h"
#include "../../../common/encoding/encoding.h"
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define IPC_PIPE_PREFIX "\\\\.\\pipe\\" IPC_PIPE_NAME

static int send_command(const char* command, const char* data, size_t data_size,
                       char* out_buffer, size_t buffer_size) {
    if (!command || !out_buffer || buffer_size == 0) {
        return -1;
    }

    HANDLE pipe = CreateFileA(
        IPC_PIPE_PREFIX,
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

int IpcGetStatus(char* out_buffer, size_t buffer_size) {
    return send_command("GET_STATUS", NULL, 0, out_buffer, buffer_size);
}

int IpcGetWhitelist(char* out_buffer, size_t buffer_size) {
    return send_command("GET_WHITELIST", NULL, 0, out_buffer, buffer_size);
}

int IpcSetWhitelist(const char* data, size_t size, char* out_buffer, size_t buffer_size) {
    return send_command("SET_WHITELIST", data, size, out_buffer, buffer_size);
}

int IpcReload(char* out_buffer, size_t buffer_size) {
    return send_command("RELOAD", NULL, 0, out_buffer, buffer_size);
}
