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
    AcquireSRWLockExclusive(&g_AllowlistLock);
    int ret = LoadWhiteList(NULL);
    ReleaseSRWLockExclusive(&g_AllowlistLock);
    PreResolveWhitelist();
    return ret;
}

static void write_str(HANDLE pipe, const char* str) {
    DWORD written;
    WriteFile(pipe, str, (DWORD)strlen(str), &written, NULL);
}

static void write_status(HANDLE pipe) {
    char buffer[IPC_BUFFER_SIZE];
    AcquireSRWLockShared(&g_AllowlistLock);
    snprintf(buffer, sizeof(buffer), "STATUS\n%zu\n%zu\nrunning\n%d\n",
             g_Whitelist.count + g_Blacklist.count, g_IpAllowlist.count, g_FiltrationEnabled);
    ReleaseSRWLockShared(&g_AllowlistLock);
    write_str(pipe, buffer);
}
static void write_ok(HANDLE pipe) {
    write_str(pipe, "OK\n");
}

static void write_error(HANDLE pipe, const char* error) {
    char buffer[IPC_BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "ERROR\n%s\n", error);
    write_str(pipe, buffer);
}

static void write_whitelist(HANDLE pipe) {
    char* buffer = malloc(IPC_MAX_MESSAGE_SIZE);
    if (!buffer) {
        return;
    }
    size_t pos = 0;
    int n = snprintf(buffer + pos, IPC_MAX_MESSAGE_SIZE - pos, "WHITELIST\n");
    if (n > 0) pos += (size_t)n;

    AcquireSRWLockShared(&g_AllowlistLock);

    // Allow rules first, then exception rules (prefixed with '!').
    for (size_t i = 0; i < g_Whitelist.count && pos < IPC_MAX_MESSAGE_SIZE - 1; i++) {
        n = snprintf(buffer + pos, IPC_MAX_MESSAGE_SIZE - pos, "%s\n", g_Whitelist.entries[i].domain);
        if (n > 0) pos += (size_t)n;
    }
    for (size_t i = 0; i < g_Blacklist.count && pos < IPC_MAX_MESSAGE_SIZE - 1; i++) {
        n = snprintf(buffer + pos, IPC_MAX_MESSAGE_SIZE - pos, "!%s\n", g_Blacklist.entries[i].domain);
        if (n > 0) pos += (size_t)n;
    }

    ReleaseSRWLockShared(&g_AllowlistLock);

    write_str(pipe, buffer);
    free(buffer);
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

            if (IpcSetWhitelist(data, data_len) == 0) {
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

        case MSG_PING:
            write_ok(pipe);
            break;

        case MSG_GET_FILTRATION: {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d\n", g_FiltrationEnabled);
            write_str(pipe, buf);
            break;
        }

        case MSG_SET_FILTRATION: {
            char* data = newline ? newline + 1 : buffer + strlen(buffer);
            int new_state = (data[0] == '1');
            g_FiltrationEnabled = new_state;
            if (new_state) {
                // Filtration turned on — clear allowlist so only IPs from
                // DNS responses received while filtration is on will be allowed.
                AcquireSRWLockExclusive(&g_AllowlistLock);
                IpAllowlistClear(&g_IpAllowlist);
                ReleaseSRWLockExclusive(&g_AllowlistLock);
            }
            PreResolveWhitelist();
            write_ok(pipe);
            break;
        }

        default:
            write_error(pipe, "unknown command");
            return -1;
    }

    return 0;
}

static DWORD WINAPI ipc_client_handler(LPVOID param) {
    HANDLE pipe = (HANDLE)param;

    // Message-mode named pipes report ERROR_MORE_DATA when a message is larger
    // than the read buffer. Read in a loop to assemble the full message so large
    // SET_WHITELIST payloads are not silently truncated.
    char* buffer = malloc(IPC_MAX_MESSAGE_SIZE + 1);
    if (!buffer) {
        CloseHandle(pipe);
        return 1;
    }

    size_t total = 0;
    DWORD bytes_read = 0;
    BOOL ok = FALSE;

    for (;;) {
        ok = ReadFile(pipe, buffer + total, (DWORD)(IPC_MAX_MESSAGE_SIZE - total), &bytes_read, NULL);
        if (ok) {
            // A successful ReadFile returns a complete message — stop here.
            // Continuing would block waiting for a second request the client
            // never sends (it is already waiting for our response).
            total += bytes_read;
            break;
        }

        if (GetLastError() != ERROR_MORE_DATA) break;
        if (bytes_read == 0) break;

        // Message larger than the buffer — append this chunk and keep reading
        // the remainder of the SAME message.
        total += bytes_read;
        if (total >= IPC_MAX_MESSAGE_SIZE) break;
    }

    if (ok && total > 0) {
        parse_and_execute(pipe, buffer, total);
    }

    free(buffer);
    FlushFileBuffers(pipe);
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);

    return 0;
}

static DWORD WINAPI ipc_server_thread(LPVOID param) {
    HANDLE stop_event = (HANDLE)(uintptr_t)param;
    HANDLE pipes[16];
    int num_pipes = 0;
    
    while (1) {
        if (num_pipes < 16) {
            HANDLE pipe = CreateNamedPipe(
                IPC_PIPE_PREFIX,
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                PIPE_UNLIMITED_INSTANCES,
                IPC_MAX_MESSAGE_SIZE,
                IPC_MAX_MESSAGE_SIZE,
                0,
                NULL
            );

            if (pipe != INVALID_HANDLE_VALUE) {
                pipes[num_pipes++] = pipe;
            }
        }
        
        if (num_pipes == 0) {
            Sleep(100);
            continue;
        }
        
        HANDLE handles[17];
        handles[0] = stop_event;
        for (int i = 0; i < num_pipes; i++) {
            handles[i + 1] = pipes[i];
        }
        
        DWORD wait_result = WaitForMultipleObjects(num_pipes + 1, handles, FALSE, 500);
        
        if (wait_result == WAIT_TIMEOUT) {
            continue;
        }
        
        if (wait_result == WAIT_OBJECT_0) {
            for (int i = 0; i < num_pipes; i++) {
                CloseHandle(pipes[i]);
            }
            break;
        }
        
        int idx = wait_result - WAIT_OBJECT_0 - 1;
        HANDLE pipe = pipes[idx];
        
        pipes[idx] = pipes[num_pipes - 1];
        num_pipes--;
        
        if (ConnectNamedPipe(pipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
            HANDLE thread = CreateThread(NULL, 0, ipc_client_handler, pipe, 0, NULL);
            if (thread) CloseHandle(thread);
        } else {
            CloseHandle(pipe);
        }
    }

    return 0;
}

int IpcStart(HANDLE stop_event) {
    // Pass the handle by value (cast through LPVOID). Passing &stop_event would hand
    // the thread a pointer to a stack local that goes out of scope on return.
    HANDLE thread = CreateThread(NULL, 0, ipc_server_thread, (LPVOID)(uintptr_t)stop_event, 0, NULL);
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
        // Empty data means clear the whitelist (block all)
        AcquireSRWLockExclusive(&g_AllowlistLock);
        WhitelistClear(&g_Whitelist);
        WhitelistClear(&g_Blacklist);
        IpAllowlistClear(&g_IpAllowlist);
        IpAllowlistClear(&g_IpBlocklist);
        ReleaseSRWLockExclusive(&g_AllowlistLock);
        return 0;
    }

    AcquireSRWLockExclusive(&g_AllowlistLock);
    WhitelistLoadFromData(&g_Whitelist, &g_Blacklist, data, size);
    // Purge allowlist entries for domains that are no longer whitelisted, so a
    // removed domain stops being reachable immediately instead of lingering for
    // the TTL. Existing still-whitelisted connections keep working.
    IpAllowlistPurgeUnowned(&g_IpAllowlist, &g_Whitelist);
    // The blocklist is exception-derived and rebuilt by PreResolveWhitelist.
    IpAllowlistClear(&g_IpBlocklist);
    ReleaseSRWLockExclusive(&g_AllowlistLock);

    PreResolveWhitelist();

    return 0;
}
