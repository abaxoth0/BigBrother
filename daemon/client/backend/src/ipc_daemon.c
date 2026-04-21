/**
 * @file ipc_daemon.c
 * @brief IPC client implementation for connecting to BigBrother Daemon.
 */

#include "../include/ipc_daemon.h"
#include "../../../common/log/log.h"
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define DAEMON_PIPE_PREFIX "\\\\.\\pipe\\" DAEMON_PIPE_NAME

#define USER_CONFIG_FILENAME "config\\user.cfg"

static char g_server_ip[64] = {0};
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

    DWORD written;
    if (!WriteFile(pipe, send_buf, (DWORD)total_len, &written, NULL)) {
        free(send_buf);
        CloseHandle(pipe);
        return -1;
    }
    free(send_buf);

    FlushFileBuffers(pipe);

    DWORD bytes_read = 0;
    if (!ReadFile(pipe, out_buffer, (DWORD)(buffer_size - 1), &bytes_read, NULL)) {
        CloseHandle(pipe);
        return -1;
    }

    out_buffer[bytes_read] = '\0';

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

int PingDaemon(void) {
    char buffer[256];
    return send_command("PING", NULL, 0, buffer, sizeof(buffer));
}

static int send_to_server(const char* command, char* out_buffer, size_t buffer_size) {
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
    
    DWORD written;
    WriteFile(pipe, command, (DWORD)strlen(command), &written, NULL);
    FlushFileBuffers(pipe);

    DWORD bytes_read = 0;
    if (!ReadFile(pipe, out_buffer, (DWORD)(buffer_size - 1), &bytes_read, NULL)) {
        printf("[send_to_server] Error: ReadFile failed\n");
        fflush(stdout);
        CloseHandle(pipe);
        return -1;
    }

    out_buffer[bytes_read] = '\0';

    // Remove trailing newlines
    while (bytes_read > 0 && (out_buffer[bytes_read - 1] == '\n' || out_buffer[bytes_read - 1] == '\r')) {
        out_buffer[--bytes_read] = '\0';
    }

    printf("[send_to_server] Response: '%s'\n", out_buffer);
    fflush(stdout);
    CloseHandle(pipe);
    return 0;
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
    
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "REGISTER:%s\n", name);
    printf("[DEBUG] ServerRegister: cmd='%s'\n", cmd);
    fflush(stdout);
    
    char response[256];
    memset(response, 0, sizeof(response));
    
    printf("[DEBUG] ServerRegister: calling send_to_server...\n");
    fflush(stdout);
    
    int result = send_to_server(cmd, response, sizeof(response));
    
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
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "CONNECT:%s\n", name);
    char response[256];
    return send_to_server(cmd, response, sizeof(response));
}

int ServerDisconnect(const char* name) {
    if (!name) return -1;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "DISCONNECT:%s\n", name);
    char response[256];
    return send_to_server(cmd, response, sizeof(response));
}

int ServerRefresh(const char* name) {
    if (!name) return -1;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "REFRESH:%s\n", name);
    char response[256];
    return send_to_server(cmd, response, sizeof(response));
}

int ServerChangeName(const char* oldName, const char* newName) {
    if (!oldName || !newName) return -1;
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "CHANGE_NAME:%s:%s\n", oldName, newName);
    char response[256];
    return send_to_server(cmd, response, sizeof(response));
}

int DaemonRun(const char* server_ip, int poll_interval_secs) {
    char whitelist_buf[8192];
    char last_whitelist[8192] = {0};
    char username[128] = {0};
    int server_connected = 0;
    int startup_retries = 30; // Wait up to 30 seconds for daemon to be ready

    LOGF("[Daemon] Waiting for local daemon to be ready...");
    while (startup_retries > 0) {
        if (PingDaemon() == 0) {
            LOGF("[Daemon] Local daemon is ready");
            break;
        }
        startup_retries--;
        Sleep(1000);
    }

    if (startup_retries == 0) {
        LOGF("[Daemon] Local daemon not responding after startup, exiting (pid: %lu)", GetCurrentProcessId());
        return 1;
    }

    // Try to connect to server on startup
    if (LoadUserName(username, sizeof(username)) == 0 && username[0] != '\0') {
        LOGF("[Daemon] Found saved username: %s, connecting to server...", username);
        if (ServerConnect(username) == 0) {
            LOGF("[Daemon] Connected to server successfully");
            server_connected = 1;
        } else {
            LOGF("[Daemon] Failed to connect to server (may not be registered/approved yet)");
        }
    } else {
        LOGF("[Daemon] No saved username found, skipping server connection");
    }

    while (1) {
        if (PingDaemon() != 0) {
            LOGF("[Daemon] Local daemon not responding, exiting (pid: %lu)", GetCurrentProcessId());
            break;
        }

        if (server_connected && username[0] != '\0') {
            // Refresh connection periodically
            ServerRefresh(username);
        }

        if (server_connected) goto wait;

        // Try to get whitelist
        char pipe_path[128];
        snprintf(pipe_path, sizeof(pipe_path), "\\\\%s\\pipe\\BigBrother.Server.Backend", server_ip);

        HANDLE pipe = CreateFile(
            pipe_path,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL
        );

        if (pipe != INVALID_HANDLE_VALUE) {
            const char* cmd = "GET_WHITELIST\n";
            DWORD written;
            WriteFile(pipe, cmd, (DWORD)strlen(cmd), &written, NULL);
            FlushFileBuffers(pipe);

            DWORD bytes_read = 0;
            if (ReadFile(pipe, whitelist_buf, sizeof(whitelist_buf) - 1, &bytes_read, NULL)) {
                whitelist_buf[bytes_read] = '\0';
                
                // Remove trailing newlines
                while (bytes_read > 0 && (whitelist_buf[bytes_read - 1] == '\n' || whitelist_buf[bytes_read - 1] == '\r')) {
                    whitelist_buf[--bytes_read] = '\0';
                }

                server_connected = 1;
                if (strcmp(whitelist_buf, last_whitelist) == 0) {
                    CloseHandle(pipe);
                    goto wait;
                }

                size_t copy_len = strlen(whitelist_buf);
                if (copy_len >= sizeof(last_whitelist)) copy_len = sizeof(last_whitelist) - 1;
                memcpy(last_whitelist, whitelist_buf, copy_len);
                last_whitelist[copy_len] = '\0';

                if (DaemonSetWhitelist(whitelist_buf, strlen(whitelist_buf), whitelist_buf, sizeof(whitelist_buf)) != 0) {
                    LOGF("[Daemon] Failed to set whitelist on daemon");
                } else {
                    LOGF("[Daemon] Whitelist updated");
                    g_whitelist_revision++;
                }
            }
            CloseHandle(pipe);
        } else {
            LOGF("[Daemon] Cannot connect to server %s, retrying...", server_ip);
            server_connected = 0;
        }
    wait:
        Sleep(poll_interval_secs * 1000);
    }

    return 0;
}
