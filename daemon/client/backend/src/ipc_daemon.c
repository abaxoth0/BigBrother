/**
 * @file ipc_daemon.c
 * @brief IPC client implementation for connecting to BigBrother Daemon.
 */

#include <winsock2.h>
#include <ws2tcpip.h>

#include "../include/ipc_daemon.h"
#include "../../../common/log/log.h"
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define DAEMON_PIPE_PREFIX "\\\\.\\pipe\\"
#define USER_CONFIG_FILENAME "config\\user.cfg"

static char g_server_ip[64] = {0};
static int g_server_session_active = 0;
static int g_registration_tried = 0;
static int g_fallback_whitelist_enabled = 1;

void SetServerSessionActive(int active) {
    g_server_session_active = active;
}

int IsServerSessionActive(void) {
    return g_server_session_active;
}

void SetFallbackWhitelistEnabled(int enabled) {
    g_fallback_whitelist_enabled = enabled ? 1 : 0;
}

int IsFallbackWhitelistEnabled(void) {
    return g_fallback_whitelist_enabled;
}

uint32_t g_whitelist_revision = 1;

#define SERVER_IP_FILENAME "config\\server.cfg"

static void get_exe_path(char* buffer, size_t size) {
    GetModuleFileName(NULL, buffer, (DWORD)size);
    char* p = buffer + strlen(buffer);
    while (p > buffer && *(p - 1) != '\\') p--;
    *p = '\0';
}

void load_server_ip(void) {
    char exe_path[MAX_PATH];
    get_exe_path(exe_path, sizeof(exe_path));

    char config_path[MAX_PATH];
    snprintf(config_path, sizeof(config_path), "%s\\" SERVER_IP_FILENAME, exe_path);

    printf("[DEBUG] load_server_ip: checking %s\n", config_path);

    FILE* f = fopen(config_path, "r");
    if (f) {
        if (fgets(g_server_ip, sizeof(g_server_ip), f)) {
            size_t len = strlen(g_server_ip);
            while (len > 0 && (g_server_ip[len-1] == '\n' || g_server_ip[len-1] == '\r')) {
                g_server_ip[--len] = '\0';
            }
            printf("[DEBUG] load_server_ip: loaded IP = '%s'\n", g_server_ip);
        }
        fclose(f);
    } else {
        printf("[DEBUG] load_server_ip: no config file\n");
    }
}

int HasServerIp(void) {
    return g_server_ip[0] != '\0';
}

void SetServerIp(const char* ip) {
    if (ip) {
        strncpy(g_server_ip, ip, sizeof(g_server_ip) - 1);

        // Save server IP to config file
        char exe_path[MAX_PATH];
        get_exe_path(exe_path, sizeof(exe_path));

        char config_path[MAX_PATH];
        snprintf(config_path, sizeof(config_path), "%s\\" SERVER_IP_FILENAME, exe_path);

        char dir_path[MAX_PATH];
        snprintf(dir_path, sizeof(dir_path), "%s\\config", exe_path);
        CreateDirectory(dir_path, NULL);

        FILE* f = fopen(config_path, "w");
        if (f) {
            fprintf(f, "%s\n", ip);
            fclose(f);
        }
    }
}

int LoadUserName(char* buffer, size_t size) {
    if (!buffer || size == 0) return -1;

    char exe_path[MAX_PATH];
    get_exe_path(exe_path, sizeof(exe_path));

    char config_path[MAX_PATH];
    snprintf(config_path, sizeof(config_path), "%s\\" USER_CONFIG_FILENAME, exe_path);

    FILE* f = fopen(config_path, "r");
    if (!f) {
        return -1;
    }

    if (!fgets(buffer, (int)size, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);

    // Remove trailing newline
    size_t len = strlen(buffer);
    while (len > 0 && (buffer[len-1] == '\n' || buffer[len-1] == '\r')) {
        buffer[--len] = '\0';
    }

    return 0;
}

int SaveUserName(const char* name) {
    if (!name) return -1;

    char exe_path[MAX_PATH];
    get_exe_path(exe_path, sizeof(exe_path));

    char config_path[MAX_PATH];
    snprintf(config_path, sizeof(config_path), "%s\\" USER_CONFIG_FILENAME, exe_path);

    // Create config directory if it doesn't exist
    char dir_path[MAX_PATH];
    snprintf(dir_path, sizeof(dir_path), "%s\\config", exe_path);
    CreateDirectory(dir_path, NULL);

    FILE* f = fopen(config_path, "w");
    if (!f) {
        return -1;
    }

    fprintf(f, "%s\n", name);
    fclose(f);
    return 0;
}

// Send command to local daemon (firewall) using old protocol format
static int send_command_tlv(const char* command, const char** args, size_t arg_count,
                           char* out_buffer, size_t buffer_size) {
    if (!command || !out_buffer || buffer_size == 0) {
        return -1;
    }

    char pipe_name[MAX_PATH];
    snprintf(pipe_name, sizeof(pipe_name), "%s%s", DAEMON_PIPE_PREFIX, DAEMON_PIPE_NAME);

    HANDLE pipe = CreateFile(
        pipe_name,
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

    // Build old-format request: command\n[args...]\n
    // For SET_WHITELIST: command + newline + data + newline
    size_t req_len = strlen(command) + 1; // command + newline
    for (size_t i = 0; i < arg_count; i++) {
        if (args[i]) {
            req_len += strlen(args[i]); // arg data directly appended
        }
    }

    char* send_buf = malloc(req_len);
    if (!send_buf) {
        CloseHandle(pipe);
        return -1;
    }

    char* p = send_buf;
    memcpy(p, command, strlen(command));
    p += strlen(command);
    *p++ = '\n';

    for (size_t i = 0; i < arg_count; i++) {
        if (args[i]) {
            memcpy(p, args[i], strlen(args[i]));
            p += strlen(args[i]);
        }
    }

    DWORD written;
    if (!WriteFile(pipe, send_buf, (DWORD)(p - send_buf), &written, NULL)) {
        free(send_buf);
        CloseHandle(pipe);
        return -1;
    }
    free(send_buf);

    FlushFileBuffers(pipe);

    // Read response using old firewall format - read entire message
    DWORD bytes_read = 0;
    if (!ReadFile(pipe, out_buffer, (DWORD)(buffer_size - 1), &bytes_read, NULL)) {
        CloseHandle(pipe);
        return -1;
    }
    out_buffer[bytes_read] = '\0';

    // Remove trailing newlines
    while (bytes_read > 0 && (out_buffer[bytes_read - 1] == '\n' || out_buffer[bytes_read - 1] == '\r')) {
        out_buffer[--bytes_read] = '\0';
    }

    CloseHandle(pipe);
    return 0;
}

int DaemonGetStatus(char* out_buffer, size_t buffer_size) {
    return send_command_tlv("GET_STATUS", NULL, 0, out_buffer, buffer_size);
}

int DaemonGetWhitelist(char* out_buffer, size_t buffer_size) {
    return send_command_tlv("GET_WHITELIST", NULL, 0, out_buffer, buffer_size);
}

int DaemonSetWhitelist(const char* data, size_t size, char* out_buffer, size_t buffer_size) {
    const char* args[1] = {data};
    return send_command_tlv("SET_WHITELIST", args, 1, out_buffer, buffer_size);
}

int DaemonReloadWhitelist(char* out_buffer, size_t buffer_size) {
    return send_command_tlv("RELOAD", NULL, 0, out_buffer, buffer_size);
}

int PingDaemon(void) {
    char buffer[256];
    return send_command_tlv("PING", NULL, 0, buffer, sizeof(buffer));
}

int DaemonGetLogPath(char* out_buffer, size_t buffer_size) {
    return send_command_tlv("GET_LOG_PATH", NULL, 0, out_buffer, buffer_size);
}

static int send_to_server_tlv(const char* command, const char** args, size_t arg_count,
                              char* out_buffer, size_t buffer_size) {
    if (!g_server_ip[0] || !command || !out_buffer || buffer_size == 0) {
        printf("[send_to_server] Error: invalid parameters or empty server IP\n");
        return -1;
    }

    char pipe_path[128];
    snprintf(pipe_path, sizeof(pipe_path), "\\\\%s\\pipe\\BigBrother.Server.Backend", g_server_ip);
    printf("[send_to_server] Connecting to: %s\n", pipe_path);
    fflush(stdout);

    HANDLE pipe = INVALID_HANDLE_VALUE;
    int retries = 3;

    while (retries > 0 && pipe == INVALID_HANDLE_VALUE) {
        pipe = CreateFile(
            pipe_path,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL
        );

        if (pipe == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            printf("[send_to_server] Attempt failed, err=%lu\n", err);

            if (err == 2 || err == 5) { // ERROR_FILE_NOT_FOUND or ERROR_ACCESS_DENIED
                printf("[send_to_server] Waiting for server...\n");
                Sleep(500);
                retries--;
            } else {
                break;
            }
        }
    }

    if (pipe == INVALID_HANDLE_VALUE) {
        printf("[send_to_server] Error: failed to connect after retries, err=%lu\n", GetLastError());
        fflush(stdout);
        return -1;
    }

    printf("[send_to_server] Connected, sending command...\n");
    fflush(stdout);

    // Build TLV request
    size_t cmd_len = strlen(command);
    size_t total_size = cmd_len + 1; // command + newline
    for (size_t i = 0; i < arg_count; i++) {
        if (args[i]) {
            total_size += snprintf(NULL, 0, "%zu", strlen(args[i])) + 1;
            total_size += strlen(args[i]) + 1;
        }
    }
    total_size += 1; // empty line

    char* send_buf = malloc(total_size);
    if (!send_buf) {
        CloseHandle(pipe);
        return -1;
    }

    char* p = send_buf;
    memcpy(p, command, cmd_len);
    p += cmd_len;
    *p++ = '\n';

    for (size_t i = 0; i < arg_count; i++) {
        if (args[i]) {
            size_t arg_len = strlen(args[i]);
            int len = snprintf(p, total_size - (p - send_buf), "%zu", arg_len);
            p += len;
            *p++ = '\n';
            memcpy(p, args[i], arg_len);
            p += arg_len;
            *p++ = '\n';
        }
    }
    *p++ = '\n';

    DWORD written;
    if (!WriteFile(pipe, send_buf, (DWORD)(p - send_buf), &written, NULL)) {
        free(send_buf);
        CloseHandle(pipe);
        return -1;
    }
    free(send_buf);

    FlushFileBuffers(pipe);

    // Read response: status\n[TLV data...\n]<empty line>
    char status_buf[32] = {0};
    char* status_out = status_buf;
    size_t status_remaining = sizeof(status_buf) - 1;
    while (status_remaining > 0) {
        char c;
        DWORD read;
        if (!ReadFile(pipe, &c, 1, &read, NULL) || read == 0) break;
        if (c == '\n') break;
        *status_out++ = c;
        status_remaining--;
    }
    *status_out = '\0';

    printf("[send_to_server] Response status: '%s'\n", status_buf);
    fflush(stdout);

    int result = -1;

    // If status is OK, read TLV data until empty line
    if (strcmp(status_buf, "OK") == 0) {
        char* out = out_buffer;
        size_t remaining = buffer_size - 1;
        int first = 1;

        while (1) {
            // Read length line
            char len_line[32] = {0};
            char* p = len_line;
            while (1) {
                char c;
                DWORD read;
                if (!ReadFile(pipe, &c, 1, &read, NULL) || read == 0) break;
                if (c == '\n') break;
                *p++ = c;
            }

            // Empty line terminates response
            if (strlen(len_line) == 0) break;

            int expected_len = atoi(len_line);
            if (expected_len < 0) break;

            // Add newline separator between values (for whitelist domains)
            if (!first) {
                if (remaining > 1) {
                    *out++ = '\n';
                    remaining--;
                }
            }
            first = 0;

            // Read value
            int count = 0;
            while (count < expected_len && remaining > 0) {
                char c;
                DWORD read;
                if (!ReadFile(pipe, &c, 1, &read, NULL) || read == 0) break;
                *out++ = c;
                count++;
                remaining--;
            }
            *out = '\0';
        }
        result = 0;
    } else if (strcmp(status_buf, "ERROR") == 0) {
        // Read error message as TLV
        char len_line[32] = {0};
        char* p = len_line;
        while (1) {
            char c;
            DWORD read;
            if (!ReadFile(pipe, &c, 1, &read, NULL) || read == 0) break;
            if (c == '\n') break;
            *p++ = c;
        }

        if (strlen(len_line) > 0) {
            int expected_len = atoi(len_line);
            if (expected_len > 0 && expected_len < (int)buffer_size) {
                DWORD bytes_read = 0;
                ReadFile(pipe, out_buffer, expected_len, &bytes_read, NULL);
                out_buffer[bytes_read] = '\0';
            }
        }
        result = -1;
    }

    CloseHandle(pipe);
    return result;
}

static int get_local_ip(const char* server_ip, char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return -1;
    buffer[0] = '\0';

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return -1;

    int ret = -1;

    // Try route-based detection using the server IP
    if (server_ip && server_ip[0]) {
        SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock != INVALID_SOCKET) {
            struct sockaddr_in addr;
            addr.sin_family = AF_INET;
            addr.sin_port = htons(445);
            addr.sin_addr.s_addr = inet_addr(server_ip);

            if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                struct sockaddr_in local_addr;
                int len = sizeof(local_addr);
                if (getsockname(sock, (struct sockaddr*)&local_addr, &len) == 0) {
                    char* ip = inet_ntoa(local_addr.sin_addr);
                    if (ip) {
                        strncpy(buffer, ip, buffer_size - 1);
                        buffer[buffer_size - 1] = '\0';
                        ret = 0;
                    }
                }
            }
            closesocket(sock);
        }
    }

    // Fallback: resolve local hostname
    if (ret != 0) {
        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) == 0) {
            struct addrinfo hints, *res = NULL;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            if (getaddrinfo(hostname, NULL, &hints, &res) == 0 && res) {
                struct sockaddr_in* sa = (struct sockaddr_in*)res->ai_addr;
                char* ip = inet_ntoa(sa->sin_addr);
                if (ip) {
                    strncpy(buffer, ip, buffer_size - 1);
                    buffer[buffer_size - 1] = '\0';
                    ret = 0;
                }
                freeaddrinfo(res);
            }
        }
    }

    WSACleanup();
    return ret;
}

int ServerRegister(const char* name) {
    if (!name) return -1;
    printf("[DEBUG] ServerRegister: START name='%s', g_server_ip='%s'\n", name, g_server_ip);
    fflush(stdout);

    if (!g_server_ip[0]) {
        printf("[DEBUG] ServerRegister: no server IP set\n");
        fflush(stdout);
        return -1;
    }

    char local_ip[64] = {0};
    get_local_ip(g_server_ip, local_ip, sizeof(local_ip));

    const char* args[2] = {name, local_ip};
    char response[256];
    memset(response, 0, sizeof(response));

    printf("[DEBUG] ServerRegister: local_ip='%s', calling send_to_server...\n", local_ip);
    fflush(stdout);

    int result = send_to_server_tlv("REGISTER", args, local_ip[0] ? 2 : 1, response, sizeof(response));

    printf("[DEBUG] ServerRegister: send_to_server done, result=%d\n", result);
    fflush(stdout);

    if (result == 0) {
        printf("[DEBUG] ServerRegister: response='%s'\n", response);
        fflush(stdout);
    }

    return result;
}

int ServerConnect(const char* name) {
    if (!name) return -1;
    char local_ip[64] = {0};
    get_local_ip(g_server_ip, local_ip, sizeof(local_ip));
    const char* args[2] = {name, local_ip};
    char response[256];
    int result = send_to_server_tlv("CONNECT", args, local_ip[0] ? 2 : 1, response, sizeof(response));
    if (result == 0) g_server_session_active = 1;
    return result;
}

int ServerDisconnect(const char* name) {
    if (!name) return -1;
    const char* args[1] = {name};
    char response[256];
    int result = send_to_server_tlv("DISCONNECT", args, 1, response, sizeof(response));
    if (result == 0) g_server_session_active = 0;
    return result;
}

int ServerRefresh(const char* name) {
    if (!name) return -1;
    const char* args[1] = {name};
    char response[256];
    int result = send_to_server_tlv("REFRESH", args, 1, response, sizeof(response));
    if (result == 0) g_server_session_active = 1;
    return result;
}

int ServerChangeName(const char* oldName, const char* newName) {
    if (!oldName || !newName) return -1;
    const char* args[2] = {oldName, newName};
    char response[256];
    return send_to_server_tlv("CHANGE_NAME", args, 2, response, sizeof(response));
}

int PingServer(void) {
    char response[256];
    return send_to_server_tlv("PING", NULL, 0, response, sizeof(response));
}

int DaemonRun(const char* server_ip, int poll_interval_secs) {
    char whitelist_buf[8192];
    char last_whitelist[8192] = {0};
    char username[128] = {0};
    int server_connected = 0;
    int startup_retries = 30; // Wait up to 30 seconds for daemon to be ready

    // Track whether we've pushed an empty whitelist for block-all mode
    int blocked_all_pushed = 0;

    LOGF("[Daemon] Waiting for local daemon to be ready...");
    int local_ready = 0;
    while (startup_retries > 0) {
        if (PingDaemon() == 0) {
            LOGF("[Daemon] Local daemon is ready");
            local_ready = 1;
            break;
        }
        startup_retries--;
        Sleep(1000);
    }

    if (!local_ready) {
        LOGF("[Daemon] Local daemon not available, continuing without it (pid: %lu)", GetCurrentProcessId());
        // Don't exit - continue in degraded mode
    }

    // Try to connect to server on startup
    if (LoadUserName(username, sizeof(username)) == 0 && username[0] != '\0') {
        LOGF("[Daemon] Found saved username: %s, connecting to server...", username);
        if (ServerConnect(username) == 0) {
            LOGF("[Daemon] Connected to server successfully");
            server_connected = 1;
        } else {
            LOGF("[Daemon] Failed to connect to server (may not be registered/approved yet)");
            if (!g_registration_tried) {
                LOGF("[Daemon] Attempting to register...");
                ServerRegister(username);
                g_registration_tried = 1;
            }
        }
    } else {
        LOGF("[Daemon] No saved username found, skipping server connection");
    }

    while (1) {
        // Check for stop event
        if (g_ServiceStopEvent != INVALID_HANDLE_VALUE) {
            if (WaitForSingleObject(g_ServiceStopEvent, 0) == WAIT_OBJECT_0) {
                LOGF("[Daemon] Stop signal received, exiting...");
                break;
            }
        }

        if (local_ready && PingDaemon() != 0) {
            LOGF("[Daemon] Local daemon not responding (pid: %lu), continuing in degraded mode", GetCurrentProcessId());
            local_ready = 0;
        }

        if (server_connected && username[0] != '\0') {
            // Refresh connection periodically
            ServerRefresh(username);
        }

        if (server_connected) goto wait;

        // Try to get whitelist from remote server
        if (username[0] != '\0') {
            const char* wl_args[1] = {username};
            char response[8192] = {0};
            if (send_to_server_tlv("GET_WHITELIST", wl_args, 1, response, sizeof(response)) == 0) {
                server_connected = 1;
                g_server_session_active = 1;
                // Also register connection on server if not already connected
                ServerConnect(username);

                // Check if server whitelist sync is disabled
                if (strcmp(response, "SYNC_DISABLED") == 0) {
                    if (strcmp(last_whitelist, "SYNC_DISABLED") != 0) {
                        if (g_fallback_whitelist_enabled) {
                            LOGF("[Daemon] Server whitelist sync is disabled, keeping local whitelist");
                        } else {
                            LOGF("[Daemon] Server whitelist sync is disabled, fallback off - blocking all traffic");
                            if (DaemonSetWhitelist("", 0, whitelist_buf, sizeof(whitelist_buf)) == 0) {
                                g_whitelist_revision++;
                                blocked_all_pushed = 1;
                            }
                        }
                        strncpy(last_whitelist, "SYNC_DISABLED", sizeof(last_whitelist) - 1);
                        last_whitelist[sizeof(last_whitelist) - 1] = '\0';
                    }
                    goto wait;
                }

                // Server returned actual whitelist entries - always push them
                if (blocked_all_pushed) {
                    blocked_all_pushed = 0;
                    last_whitelist[0] = '\0'; // Force push to restore from block-all
                }
                if (strcmp(response, last_whitelist) == 0) {
                    goto wait;
                }
                size_t copy_len = strlen(response);
                if (copy_len >= sizeof(last_whitelist)) copy_len = sizeof(last_whitelist) - 1;
                memcpy(last_whitelist, response, copy_len);
                last_whitelist[copy_len] = '\0';

                if (DaemonSetWhitelist(response, strlen(response), whitelist_buf, sizeof(whitelist_buf)) != 0) {
                    LOGF("[Daemon] Failed to set whitelist on daemon");
                } else {
                    LOGF("[Daemon] Whitelist updated");
                    g_whitelist_revision++;
                }
            } else {
                LOGF("[Daemon] Cannot connect to server %s, retrying...", g_server_ip);
                server_connected = 0;
                g_server_session_active = 0;
                // Push empty whitelist if fallback is disabled and not already blocked
                if (!g_fallback_whitelist_enabled && local_ready && !blocked_all_pushed) {
                    LOGF("[Daemon] Server unreachable and fallback off - blocking all traffic");
                    if (DaemonSetWhitelist("", 0, whitelist_buf, sizeof(whitelist_buf)) == 0) {
                        g_whitelist_revision++;
                        blocked_all_pushed = 1;
                    }
                }
                if (!g_registration_tried) {
                    LOGF("[Daemon] Attempting to register...");
                    ServerRegister(username);
                    g_registration_tried = 1;
                }
            }
        } else {
            LOGF("[Daemon] No username, skipping server whitelist fetch");
            goto wait;
        }

        wait:
        // Sleep in small increments to check for stop
        for (int i = 0; i < poll_interval_secs; i++) {
            if (g_ServiceStopEvent != INVALID_HANDLE_VALUE) {
                if (WaitForSingleObject(g_ServiceStopEvent, 0) == WAIT_OBJECT_0) {
                    LOGF("[Daemon] Stop signal received during sleep, exiting...");
                    goto stop;
                }
            }
            Sleep(1000);
        }
    }

stop:
    return 0;
}
