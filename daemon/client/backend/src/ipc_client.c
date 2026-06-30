/**
 * @file ipc_client.c
 * @brief Named pipe server for Client Frontend & Backend communication.
 */

#include "../include/ipc_client.h"
#include "../include/ipc_daemon.h"
#include "../include/net_client.h"
#include "../../../common/log/log.h"
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
        int server_ok = IsServerSessionActive();

        char client_name[128] = {0};
        LoadUserName(client_name, sizeof(client_name));

        // data[0] = ClientName, data[1] = IpAddress, data[2] = DaemonStatus
        // data[3] = Backend status ("running"), data[4] = WhitelistRevision
        // data[5] = PID, data[6] = ServerRunning, data[7] = ServerSession
        // data[8] = FiltrationEnabled
        char pid_str[32];
        snprintf(pid_str, sizeof(pid_str), "%lu", GetCurrentProcessId());

        char rev_str[32];
        snprintf(rev_str, sizeof(rev_str), "%u", g_whitelist_revision);

        char filt_str[8];
        snprintf(filt_str, sizeof(filt_str), "%d", DaemonGetFiltration());

        const char* data[9] = {
            client_name[0] ? client_name : "unknown",
            "127.0.0.1",
            daemon_ok ? "running" : "not_running",
            "running",
            rev_str,
            pid_str,
            server_ok ? "running" : "not_running",
            IsServerSessionActive() ? "connected" : "not_connected",
            filt_str
        };
        write_response_tlv(pipe, "OK", data, 9);

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
        } else {
            int r = ServerConnect(username);
            if (r == 0) {
                write_ok(pipe);
            } else {
                char err_msg[256];
                snprintf(err_msg, sizeof(err_msg), "connect failed: server error (or see log)");
                write_error_tlv(pipe, err_msg);
            }
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

    } else if (strcmp(buffer, "SET_FALLBACK_WHITELIST") == 0) {
        if (arg_count < 1 || !args[0]) {
            write_error_tlv(pipe, "missing value (0 or 1)");
        } else {
            SetFallbackWhitelistEnabled(args[0][0] == '1');
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "GET_FALLBACK_WHITELIST") == 0) {
        const char* val = IsFallbackWhitelistEnabled() ? "1" : "0";
        const char* data[1] = {val};
        write_response_tlv(pipe, "OK", data, 1);

    } else if (strcmp(buffer, "SET_SERVER_ADDR") == 0) {
        if (arg_count < 1 || !args[0] || strlen(args[0]) == 0) {
            write_error_tlv(pipe, "missing server address");
        } else {
            const char* new_ip = args[0];
            // If this is the local machine's IP, use "." (localhost TCP)
            if (strcmp(new_ip, "127.0.0.1") != 0 && strcmp(new_ip, ".") != 0) {
                char local_ip[64] = {0};
                GetLocalIp(new_ip, local_ip, sizeof(local_ip));
                if (local_ip[0] && strcmp(new_ip, local_ip) == 0) {
                    SetServerIp(".");
                    write_ok(pipe);
                    return 0;
                }
            }
            SetServerIp(args[0]);
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "GET_SERVER_ADDR") == 0) {
        const char* addr = GetServerIp();
        const char* data[1] = {addr ? addr : ""};
        write_response_tlv(pipe, "OK", data, 1);

    } else if (strcmp(buffer, "SET_USERNAME") == 0) {
        if (arg_count < 1 || !args[0] || strlen(args[0]) == 0) {
            write_error_tlv(pipe, "missing username");
        } else {
            SaveUserName(args[0]);
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "GET_USERNAME") == 0) {
        char buf[128] = {0};
        const char* data[1] = {buf};
        if (LoadUserName(buf, sizeof(buf)) == 0) {
            write_response_tlv(pipe, "OK", data, 1);
        } else {
            write_response_tlv(pipe, "OK", data, 1); // empty string = not set
        }

    } else if (strcmp(buffer, "GET_FILTRATION") == 0) {
        char val[8];
        int enabled = IsFiltrationEnabled();
        snprintf(val, sizeof(val), "%d", enabled);
        const char* data[1] = {val};
        write_response_tlv(pipe, "OK", data, 1);

    } else if (strcmp(buffer, "SET_FILTRATION") == 0) {
        if (arg_count < 1 || !args[0]) {
            write_error_tlv(pipe, "missing value (0 or 1)");
        } else {
            int enabled = (args[0][0] == '1');
            if (DaemonSetFiltration(enabled) == 0) {
                write_ok(pipe);
            } else {
                write_error_tlv(pipe, "failed to set filtration");
            }
        }

    } else if (strcmp(buffer, "DISCOVER_SERVERS") == 0) {
        int timeout_ms = 2000;
        if (arg_count >= 1 && args[0]) {
            timeout_ms = atoi(args[0]);
            if (timeout_ms <= 0) timeout_ms = 2000;
        }

        char local_ip[64] = {0};
        char bcast_list[4096] = {0};

        GetAllBroadcastAddresses(local_ip, sizeof(local_ip), bcast_list, sizeof(bcast_list));
        {
            char response[8192] = {0};
            int count = DiscoverServers(bcast_list, 42069, timeout_ms, response, sizeof(response));
            if (count == 0) {
                LOGF("[Discovery] UDP returned 0, trying TCP subnet scan...");
                count = DiscoverServersTCP(response, sizeof(response), timeout_ms);
            }

            if (count > 0) {
                LOGF("[Discovery] local_ip='%s', first result: %s", local_ip, response);
                // Replace server IP with "." if it matches local machine IP (use local loopback TCP)
                if (local_ip[0] != '\0') {
                    char* line = response;
                    while (line && *line) {
                        char* pipe_c = strchr(line, '|');
                        if (pipe_c) {
                            char* ip = pipe_c + 1;
                            char* nl = strchr(ip, '\n');
                            if (nl) *nl = '\0';
                            LOGF("[Discovery] Checking IP='%s' vs local_ip='%s'", ip, local_ip);
                            if (strcmp(ip, local_ip) == 0) {
                                LOGF("[Discovery] Replacing %s with '.'", ip);
                                // Replace IP with "." for localhost TCP
                                memmove(pipe_c + 2, pipe_c + strlen(pipe_c + 1) + 1,
                                    strlen(pipe_c + 1) + 1);
                                pipe_c[1] = '.';
                            }
                            if (nl) *nl = '\n';
                        }
                        char* next = strchr(line, '\n');
                        line = next ? next + 1 : NULL;
                    }
                }

                // Parse "name|ip|port\n..." lines into TLV values
                char* lines[DISCOVERY_MAX_SERVERS];
                int line_count = 0;
                char* p = response;
                while (p && *p && line_count < DISCOVERY_MAX_SERVERS) {
                    lines[line_count++] = p;
                    char* nl = strchr(p, '\n');
                    if (nl) {
                        *nl = '\0';
                        p = nl + 1;
                    } else {
                        break;
                    }
                }
                write_response_tlv(pipe, "OK", (const char**)lines, line_count);
            } else {
                write_error_tlv(pipe, "no servers found");
            }
        }

    } else if (strcmp(buffer, "SET_SERVER_PORT") == 0) {
        if (arg_count < 1 || !args[0] || strlen(args[0]) == 0) {
            write_error_tlv(pipe, "missing port number");
        } else {
            ini_set_string("server", "port", args[0]);
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "GET_SERVER_PORT") == 0) {
        char buf[16] = {0};
        int port = get_server_port();
        snprintf(buf, sizeof(buf), "%d", port);
        const char* data[1] = {buf};
        write_response_tlv(pipe, "OK", data, 1);

    } else if (strcmp(buffer, "SET_DISCOVERY_ENABLED") == 0) {
        if (arg_count < 1 || !args[0]) {
            write_error_tlv(pipe, "missing value (0 or 1)");
        } else {
            ini_set_string("discovery", "enabled", args[0]);
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "GET_DISCOVERY_ENABLED") == 0) {
        const char* val = is_discovery_enabled() ? "1" : "0";
        const char* data[1] = {val};
        write_response_tlv(pipe, "OK", data, 1);

    } else if (strcmp(buffer, "SET_NETWORK_AUTO") == 0) {
        if (arg_count < 1 || !args[0]) {
            write_error_tlv(pipe, "missing value (0 or 1)");
        } else {
            ini_set_string("network", "auto", args[0]);
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "GET_NETWORK_AUTO") == 0) {
        char buf[8] = {0};
        int auto_val = 1;
        if (ini_get_string("network", "auto", buf, sizeof(buf)) && buf[0])
            auto_val = (buf[0] == '1' || buf[0] == 't' || buf[0] == 'y');
        const char* data[1] = {auto_val ? "1" : "0"};
        write_response_tlv(pipe, "OK", data, 1);

    } else if (strcmp(buffer, "SET_NETWORK_GATEWAY") == 0) {
        if (arg_count < 1 || !args[0] || strlen(args[0]) == 0) {
            write_error_tlv(pipe, "missing gateway address");
        } else {
            ini_set_string("network", "gateway", args[0]);
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "GET_NETWORK_GATEWAY") == 0) {
        char buf[64] = {0};
        ini_get_string("network", "gateway", buf, sizeof(buf));
        const char* data[1] = {buf};
        write_response_tlv(pipe, "OK", data, 1);

    } else if (strcmp(buffer, "SET_NETWORK_MASK") == 0) {
        if (arg_count < 1 || !args[0] || strlen(args[0]) == 0) {
            write_error_tlv(pipe, "missing subnet mask");
        } else {
            ini_set_string("network", "mask", args[0]);
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "GET_NETWORK_MASK") == 0) {
        char buf[64] = {0};
        ini_get_string("network", "mask", buf, sizeof(buf));
        const char* data[1] = {buf};
        write_response_tlv(pipe, "OK", data, 1);

    } else if (strcmp(buffer, "SET_SERVER_NAME") == 0) {
        if (arg_count < 1 || !args[0] || strlen(args[0]) == 0) {
            write_error_tlv(pipe, "missing server name");
        } else {
            ini_set_string("server", "name", args[0]);
            write_ok(pipe);
        }

    } else if (strcmp(buffer, "GET_SERVER_NAME") == 0) {
        char buf[128] = {0};
        if (ini_get_string("server", "name", buf, sizeof(buf)) && buf[0]) {
            const char* data[1] = {buf};
            write_response_tlv(pipe, "OK", data, 1);
        } else {
            write_error_tlv(pipe, "no server name set");
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
