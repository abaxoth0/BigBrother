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

#define CLIENT_PIPE_NAME "\\\\.\\pipe\\BigBrother.Client.Backend"
#define CLIENT_PIPE_BUFFER_SIZE 4096

static HANDLE g_shutdown_event = NULL;

void SignalClientServerShutdown(void) {
    if (g_shutdown_event) {
        SetEvent(g_shutdown_event);
    }
}

// Write response: status\n[TLV data...\n]<empty line>
static void write_response_tlv(HANDLE pipe, const char* status, const char** data, size_t data_count) {
    DWORD written;
    // Write status line
    WriteFile(pipe, status, (DWORD)strlen(status), &written, NULL);
    WriteFile(pipe, "\n", 1, &written, NULL);

    if (data && data_count > 0) {
        for (size_t i = 0; i < data_count; i++) {
            if (data[i]) {
                // Write length line
                char len_buf[32];
                int len = snprintf(len_buf, sizeof(len_buf), "%zu", strlen(data[i]));
                WriteFile(pipe, len_buf, (DWORD)len, &written, NULL);
                WriteFile(pipe, "\n", 1, &written, NULL);
                // Write value line
                WriteFile(pipe, data[i], (DWORD)strlen(data[i]), &written, NULL);
                WriteFile(pipe, "\n", 1, &written, NULL);
            }
        }
    }
    // Empty line terminates response
    WriteFile(pipe, "\n", 1, &written, NULL);
    FlushFileBuffers(pipe);
}

static void write_error_tlv(HANDLE pipe, const char* error) {
    if (!error) error = "Unknown error";
    const char* data[1] = {error};
    write_response_tlv(pipe, "ERROR", data, 1);
}

static void write_ok(HANDLE pipe) {
    write_response_tlv(pipe, "OK", NULL, 0);
}

// Read TLV request: command\n<len>\n<arg>\n...\n<empty line>
// Returns command in buffer, args in args array (caller must free)
// Returns number of args, or -1 on error
static int read_tlv_request(HANDLE pipe, char* cmd_buf, size_t cmd_size,
                                char*** args_out) {
    DWORD bytes_read;
    size_t arg_count = 0;
    char** args = NULL;

    // Read command line byte by byte until newline
    char* p = cmd_buf;
    size_t remaining = cmd_size - 1;
    int last_was_cr = 0;
    while (remaining > 0) {
        char c;
        if (!ReadFile(pipe, &c, 1, &bytes_read, NULL) || bytes_read == 0) {
            return -1;
        }
        if (c == '\r') {
            last_was_cr = 1;
            continue;
        }
        if (c == '\n') {
            // If last was CR, this is \r\n - we already got the newline
            if (last_was_cr) {
                last_was_cr = 0;
            }
            break;
        }
        last_was_cr = 0;
        *p++ = c;
        remaining--;
    }
    *p = '\0';

    // Read arguments until empty line
    while (1) {
        // Read length line
        char len_line[32] = {0};
        char* p = len_line;
        int last_was_cr = 0;
        int is_empty_line = 0;
        while (1) {
            char c;
            if (!ReadFile(pipe, &c, 1, &bytes_read, NULL) || bytes_read == 0) {
                goto error;
            }
            if (c == '\r') {
                last_was_cr = 1;
                continue;
            }
            if (c == '\n') {
                if (last_was_cr) {
                    // This is \r\n - if line is empty, it's the terminator
                    if (strlen(len_line) == 0) {
                        is_empty_line = 1;
                    }
                    last_was_cr = 0;
                    break;
                }
                break;
            }
            last_was_cr = 0;
            *p++ = c;
        }

        // Empty line terminates request (or \r\n)
        if (is_empty_line || strlen(len_line) == 0) break;

        int expected_len = atoi(len_line);
        if (expected_len < 0) goto error;

        // Read value bytes
        char* value = malloc(expected_len + 1);
        if (!value) goto error;
        if (!ReadFile(pipe, value, (DWORD)expected_len, &bytes_read, NULL) || bytes_read != (DWORD)expected_len) {
            free(value);
            goto error;
        }
        value[expected_len] = '\0';

        // Read newline after value (may be \n or \r\n)
        char nl;
        if (ReadFile(pipe, &nl, 1, &bytes_read, NULL) && bytes_read == 1) {
            if (nl == '\r') {
                // Skip the following \n
                ReadFile(pipe, &nl, 1, &bytes_read, NULL);
            }
        }

        // Add to args
        char** new_args = realloc(args, (arg_count + 1) * sizeof(char*));
        if (!new_args) {
            free(value);
            goto error;
        }
        args = new_args;
        args[arg_count++] = value;
    }

    *args_out = args;
    return (int)arg_count;

error:
    if (args) {
        for (size_t i = 0; i < arg_count; i++) free(args[i]);
        free(args);
    }
    return -1;
}

DWORD WINAPI client_handler(LPVOID param) {
    HANDLE pipe = (HANDLE)param;
    char buffer[CLIENT_PIPE_BUFFER_SIZE];

    // Read TLV request
    char** args = NULL;
    int arg_count = read_tlv_request(pipe, buffer, sizeof(buffer), &args);

    if (arg_count < 0) {
        write_error_tlv(pipe, "failed to parse request");
        CloseHandle(pipe);
        return 1;
    }

    // Parse command
    if (strcmp(buffer, "GET_STATUS") == 0) {
        int daemon_ok = (PingDaemon() == 0);

        // Get client name from config
        char client_name[128] = {0};
        LoadUserName(client_name, sizeof(client_name));

        // Build response with separate TLV values
        // data[0] = ClientName, data[1] = IpAddress, data[2] = DaemonStatus
        // data[3] = Backend status ("running"), data[4] = WhitelistRevision
        char rev_str[32];
        snprintf(rev_str, sizeof(rev_str), "%u", g_whitelist_revision);

        const char* data[5] = {
            client_name[0] ? client_name : "unknown",
            "127.0.0.1",
            daemon_ok ? "running" : "not_running",
            "running",
            rev_str
        };
        write_response_tlv(pipe, "OK", data, 5);

    } else if (strcmp(buffer, "GET_WHITELIST") == 0) {
        char whitelist_buf[8192];
        if (DaemonGetWhitelist(whitelist_buf, sizeof(whitelist_buf)) != 0) {
            write_error_tlv(pipe, "failed to get whitelist");
        } else {
            // Parse firewall response: "WHITELIST\ndomain1\ndomain2\n..."
            // Skip "WHITELIST" header line
            char* line = strchr(whitelist_buf, '\n');
            if (!line) {
                write_error_tlv(pipe, "invalid whitelist format");
            } else {
                char* domains[256];
                int domain_count = 0;
                line++; // skip past newline

                while (line && *line && domain_count < 256) {
                    char* next_line = strchr(line, '\n');
                    if (next_line) *next_line = '\0';
                    if (strlen(line) > 0) {
                        domains[domain_count++] = line;
                    }
                    line = next_line ? next_line + 1 : NULL;
                }

                write_response_tlv(pipe, "OK", (const char**)domains, domain_count);
            }
        }

    } else if (strcmp(buffer, "RELOAD_WHITELIST") == 0) {
        char response[256];
        DaemonReloadWhitelist(response, sizeof(response));
        write_ok(pipe);

    } else if (strcmp(buffer, "RESTART_CLIENT") == 0) {
        write_ok(pipe);
        CloseHandle(pipe);
        ExitProcess(0);

    } else if (strcmp(buffer, "PING") == 0) {
        write_ok(pipe);

    } else if (strcmp(buffer, "GET_LOG_PATH") == 0) {
        char exe_path[MAX_PATH];
        char* name = exe_path + GetModuleFileName(NULL, exe_path, MAX_PATH);
        while (name > exe_path && *(name - 1) != '\\') name--;
        *name = '\0';

        char client_log[MAX_PATH];
        snprintf(client_log, sizeof(client_log), "%s\\logs\\client.binlog", exe_path);

        char firewall_log[MAX_PATH];
        snprintf(firewall_log, sizeof(firewall_log), "%s\\logs\\firewall.binlog", exe_path);

        const char* data[2] = {client_log, firewall_log};
        write_response_tlv(pipe, "OK", data, 2);

    } else if (strcmp(buffer, "REGISTER") == 0) {
        if (arg_count < 1 || !args[0] || strlen(args[0]) == 0) {
            write_error_tlv(pipe, "missing username");
        } else if (ServerRegister(args[0]) != 0) {
            write_error_tlv(pipe, "failed to register");
        } else {
            SaveUserName(args[0]);
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "CONNECT") == 0) {
        char username[128];
        if (LoadUserName(username, sizeof(username)) != 0) {
            write_error_tlv(pipe, "no saved username");
        } else if (ServerConnect(username) != 0) {
            write_error_tlv(pipe, "failed to connect (not registered/approved)");
        } else {
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "DISCONNECT") == 0) {
        char username[128];
        if (LoadUserName(username, sizeof(username)) != 0) {
            write_error_tlv(pipe, "no saved username");
        } else if (ServerDisconnect(username) != 0) {
            write_error_tlv(pipe, "failed to disconnect");
        } else {
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "REFRESH") == 0) {
        char username[128];
        if (LoadUserName(username, sizeof(username)) != 0) {
            write_error_tlv(pipe, "not connected");
        } else if (ServerRefresh(username) != 0) {
            write_error_tlv(pipe, "failed to refresh");
        } else {
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "CHANGE_NAME") == 0) {
        if (arg_count < 1 || !args[0] || strlen(args[0]) == 0) {
            write_error_tlv(pipe, "missing new name");
        } else {
            char old_name[128];
            if (LoadUserName(old_name, sizeof(old_name))) {
                write_error_tlv(pipe, "no saved username");
            } else if (ServerChangeName(old_name, args[0]) != 0) {
                write_error_tlv(pipe, "failed to change name");
            } else {
                SaveUserName(args[0]);
                write_ok(pipe);
            }
        }

    } else {
        write_error_tlv(pipe, "unknown command");
    }

    // Free args
    if (args) {
        for (int i = 0; i < arg_count; i++) free(args[i]);
        free(args);
    }

    CloseHandle(pipe);
    return 0;
}

DWORD WINAPI client_server_thread(LPVOID param) {
    (void)param;

    g_shutdown_event = CreateEvent(NULL, TRUE, FALSE, NULL);

    while (1) {
        HANDLE pipe = CreateNamedPipe(
            CLIENT_PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
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

        // Check for shutdown before waiting for connection
        if (WaitForSingleObject(g_shutdown_event, 0) == WAIT_OBJECT_0) {
            CloseHandle(pipe);
            break;
        }

        // Use timeout-based approach to allow shutdown
        if (ConnectNamedPipe(pipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
            // Check again for shutdown
            if (WaitForSingleObject(g_shutdown_event, 0) == WAIT_OBJECT_0) {
                CloseHandle(pipe);
                break;
            }
            HANDLE thread = CreateThread(NULL, 0, client_handler, pipe, 0, NULL);
            if (thread) {
                CloseHandle(thread);
            }
        } else {
            CloseHandle(pipe);
        }

        // Small sleep to prevent CPU spinning
        Sleep(100);
    }

    return 0;
}

void StartClientServer(void) {
    HANDLE thread = CreateThread(NULL, 0, client_server_thread, NULL, 0, NULL);
    if (thread) {
        CloseHandle(thread);
    }
}
