/**
 * @file ipc_daemon.c
 * @brief IPC client implementation for connecting to BigBrother Daemon.
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include "../include/ipc_daemon.h"
#include "../include/ipc_client.h"
#include "../../../common/log/log.h"
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define DAEMON_PIPE_PREFIX "\\\\.\\pipe\\"
#define CONFIG_INI_PATH "config\\config.ini"
#define INI_LINE_MAX 512

#define SRV_HEARTBEAT_INTERVAL_MS 10000
#define SRV_LOCAL_DAEMON_CHECK_MS 10000

static char g_server_ip[64] = {0};
static int g_server_session_active = 0;
static int g_registration_tried = 0;
static int g_fallback_whitelist_enabled = 1;
static int g_filtration_enabled = 1;

static char g_last_whitelist[8192] = {0};
static int g_blocked_all_pushed = 0;

void SetServerSessionActive(int active) {
    if (g_server_session_active != active) {
        g_server_session_active = active;
        NotifyStateChanged();
    }
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

static void get_exe_path(char* buffer, size_t size) {
    GetModuleFileName(NULL, buffer, (DWORD)size);
    char* p = buffer + strlen(buffer);
    while (p > buffer && *(p - 1) != '\\') p--;
    *p = '\0';
}

static void build_ini_path(char* buffer, size_t size) {
    get_exe_path(buffer, size);
    size_t len = strlen(buffer);
    snprintf(buffer + len, size - len, "\\" CONFIG_INI_PATH);
}

static char* trim_ws(char* s) {
    while (*s == ' ' || *s == '\t') s++;
    char* end = s + strlen(s);
    while (end > s && (*(end-1) == ' ' || *(end-1) == '\t' || *(end-1) == '\r' || *(end-1) == '\n')) end--;
    *end = '\0';
    return s;
}

// Read string value from config.ini.
// Returns 1 if found, 0 if not found.
int ini_get_string(const char* section, const char* key, char* out, size_t out_size) {
    if (!out || out_size == 0) return 0;
    out[0] = '\0';

    char ini_path[MAX_PATH];
    build_ini_path(ini_path, sizeof(ini_path));

    // Ensure config directory exists
    {
        char dir[MAX_PATH];
        get_exe_path(dir, sizeof(dir));
        strncat(dir, "\\config", sizeof(dir) - strlen(dir) - 1);
        CreateDirectory(dir, NULL);
    }

    FILE* f = fopen(ini_path, "r");
    if (!f) {
        // Create empty config file with header
        f = fopen(ini_path, "w");
        if (f) {
            fprintf(f, "; BigBrother configuration file\n");
            fclose(f);
        }
        return 0;
    }

    char line[INI_LINE_MAX];
    int in_section = 0;

    while (fgets(line, sizeof(line), f)) {
        char* p = trim_ws(line);
        if (p[0] == '\0' || p[0] == ';' || p[0] == '#') continue;

        if (p[0] == '[') {
            char* end = strchr(p, ']');
            if (!end) continue;
            *end = '\0';
            in_section = (_stricmp(p + 1, section) == 0);
            continue;
        }

        if (!in_section) continue;

        char* eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char* k = trim_ws(p);
        char* v = trim_ws(eq + 1);

        if (_stricmp(k, key) == 0) {
            strncpy(out, v, out_size - 1);
            out[out_size - 1] = '\0';
            fclose(f);
            return 1;
        }
    }

    fclose(f);
    return 0;
}

// Write or update a string value in config.ini.
// Retains all other sections and keys.
int ini_set_string(const char* section, const char* key, const char* value) {
    char ini_path[MAX_PATH];
    build_ini_path(ini_path, sizeof(ini_path));

    // Ensure config directory exists
    {
        char dir[MAX_PATH];
        get_exe_path(dir, sizeof(dir));
        strncat(dir, "\\config", sizeof(dir) - strlen(dir) - 1);
        CreateDirectory(dir, NULL);
    }

    // Read all lines
    char** lines = NULL;
    int line_count = 0;

    FILE* f = fopen(ini_path, "r");
    if (f) {
        char buf[INI_LINE_MAX];
        while (fgets(buf, sizeof(buf), f)) {
            char* copy = _strdup(buf);
            if (copy) {
                char** new_lines = realloc(lines, (line_count + 1) * sizeof(char*));
                if (new_lines) {
                    lines = new_lines;
                    lines[line_count++] = copy;
                } else {
                    free(copy);
                }
            }
        }
        fclose(f);
    }

    // Walk lines to find section and key (use temp copy to preserve original for writing)
    int section_idx = -1;
    int key_idx = -1;
    int last_in_section = -1;

    for (int i = 0; i < line_count; i++) {
        char tmp[INI_LINE_MAX];
        strncpy(tmp, lines[i], sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        char* p = trim_ws(tmp);
        if (p[0] == '\0' || p[0] == ';' || p[0] == '#') continue;

        if (p[0] == '[') {
            char* end = strchr(p, ']');
            if (!end) continue;
            *end = '\0';
            if (_stricmp(p + 1, section) == 0) {
                section_idx = i;
            } else if (section_idx >= 0 && last_in_section < 0) {
                last_in_section = i - 1;
            }
            continue;
        }

        if (section_idx >= 0) {
            char* eq = strchr(p, '=');
            if (eq) {
                *eq = '\0';
                char* k = trim_ws(p);
                if (_stricmp(k, key) == 0) {
                    key_idx = i;
                }
            }
            last_in_section = i;
        }
    }

    char new_line[INI_LINE_MAX];
    snprintf(new_line, sizeof(new_line), "%s=%s\n", key, value ? value : "");

    // Build new content
    // We'll write directly to a temp file, then replace
    char tmp_path[MAX_PATH];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", ini_path);

    FILE* out = fopen(tmp_path, "w");
    if (!out) {
        for (int i = 0; i < line_count; i++) free(lines[i]);
        free(lines);
        return -1;
    }

    if (line_count == 0) {
        // Empty file — write section header + key
        fprintf(out, "[%s]\n%s", section, new_line);
    } else if (key_idx >= 0) {
        // Key exists — replace value line
        for (int i = 0; i < line_count; i++) {
            if (i == key_idx) {
                fputs(new_line, out);
            } else {
                fputs(lines[i], out);
            }
        }
    } else if (section_idx >= 0) {
        // Section exists but key doesn't — append after last key in section
        for (int i = 0; i < line_count; i++) {
            fputs(lines[i], out);
            if (i == last_in_section) {
                fputs(new_line, out);
            }
        }
    } else {
        // No section — append at end
        for (int i = 0; i < line_count; i++) {
            fputs(lines[i], out);
        }
        fprintf(out, "\n[%s]\n%s", section, new_line);
    }

    fclose(out);

    // Replace original
    remove(ini_path);
    rename(tmp_path, ini_path);

    for (int i = 0; i < line_count; i++) free(lines[i]);
    free(lines);
    return 0;
}

void load_server_ip(void) {
    char buf[64] = {0};
    if (ini_get_string("server", "address", buf, sizeof(buf)) && buf[0]) {
        strncpy(g_server_ip, buf, sizeof(g_server_ip) - 1);
        printf("[DEBUG] load_server_ip: loaded IP = '%s'\n", g_server_ip);
    } else {
        printf("[DEBUG] load_server_ip: no config or empty\n");
    }
}

const char* GetServerIp(void) {
    return g_server_ip;
}

int HasServerIp(void) {
    return g_server_ip[0] != '\0';
}

void SetServerIp(const char* ip) {
    if (ip) {
        strncpy(g_server_ip, ip, sizeof(g_server_ip) - 1);
        ini_set_string("server", "address", ip);
    }
}

int LoadUserName(char* buffer, size_t size) {
    if (!buffer || size == 0) return -1;
    if (ini_get_string("client", "username", buffer, size) && buffer[0]) {
        return 0;
    }
    return -1;
}

int SaveUserName(const char* name) {
    if (!name) return -1;
    return ini_set_string("client", "username", name);
}

#include "../include/net_client.h"

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

    // Read response using old firewall format. The firewall pipe is message
    // mode with up to 64KB messages; a single byte-mode ReadFile truncates
    // large responses (e.g. a >8KB whitelist dump). Switch to message read
    // mode and loop on ERROR_MORE_DATA to assemble the full response.
    DWORD pipe_mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(pipe, &pipe_mode, NULL, NULL);

    size_t total = 0;
    DWORD bytes_read = 0;
    BOOL ok = FALSE;

    for (;;) {
        if (total >= buffer_size - 1) break;
        ok = ReadFile(pipe, out_buffer + total, (DWORD)(buffer_size - 1 - total), &bytes_read, NULL);
        if (ok) {
            total += bytes_read;
            break; // complete message read
        }
        if (GetLastError() != ERROR_MORE_DATA) break;
        if (bytes_read == 0) break;
        total += bytes_read;
        if (total >= buffer_size - 1) break;
    }

    if (!ok && total == 0) {
        CloseHandle(pipe);
        return -1;
    }
    out_buffer[total] = '\0';

    // Remove trailing newlines
    while (total > 0 && (out_buffer[total - 1] == '\n' || out_buffer[total - 1] == '\r')) {
        out_buffer[--total] = '\0';
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

int DaemonGetFiltration(void) {
    char buf[16] = {0};
    if (send_command_tlv("GET_FILTRATION", NULL, 0, buf, sizeof(buf)) == 0) {
        g_filtration_enabled = (buf[0] == '1');
    }
    return g_filtration_enabled;
}

int DaemonSetFiltration(int enabled) {
    const char* args[1] = {enabled ? "1" : "0"};
    char buf[16] = {0};
    if (send_command_tlv("SET_FILTRATION", args, 1, buf, sizeof(buf)) == 0) {
        g_filtration_enabled = enabled;
        NotifyStateChanged();
        return 0;
    }
    return -1;
}

int IsFiltrationEnabled(void) {
    return g_filtration_enabled;
}

int IsFiltrationAutoDisableEnabled(void) {
    char buf[8] = {0};
    if (ini_get_string("filtration", "auto_disable", buf, sizeof(buf)) && buf[0])
        return buf[0] == '1';
    return 0;
}

void SetFiltrationAutoDisableEnabled(int enabled) {
    ini_set_string("filtration", "auto_disable", enabled ? "1" : "0");
}

static void apply_auto_disable(void) {
    if (IsFiltrationAutoDisableEnabled() && IsFiltrationEnabled()) {
        LOGF("[Daemon] Auto-disable: disabling filtration (server disconnected)");
        DaemonSetFiltration(0);
    }
}

static void apply_auto_enable(void) {
    if (IsFiltrationAutoDisableEnabled() && !IsFiltrationEnabled()) {
        LOGF("[Daemon] Auto-disable: enabling filtration (server connected)");
        DaemonSetFiltration(1);
    }
}

static int send_to_server_tlv(const char* command, const char** args, size_t arg_count,
                              char* out_buffer, size_t buffer_size) {
    if (!g_server_ip[0] || !command || !out_buffer || buffer_size == 0) {
        printf("[send_to_server] Error: invalid parameters or empty server IP\n");
        return -1;
    }

    // Ensure Winsock is initialized
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("[send_to_server] WSAStartup failed\n");
        return -1;
    }

    // Resolve target IP: "." or "127.0.0.1" means localhost
    const char* target_ip = g_server_ip;
    if (strcmp(g_server_ip, ".") == 0 || strcmp(g_server_ip, "127.0.0.1") == 0) {
        target_ip = "127.0.0.1";
    }

    int port = get_server_port();
    printf("[send_to_server] Connecting to %s:%d...\n", target_ip, port);
    fflush(stdout);

    // TCP socket
    SOCKET sock = INVALID_SOCKET;
    int retries = 10;

    while (retries > 0 && sock == INVALID_SOCKET) {
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            printf("[send_to_server] socket() failed: %lu\n", (unsigned long)WSAGetLastError());
            WSACleanup();
            return -1;
        }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons((unsigned short)port);
        inet_pton(AF_INET, target_ip, &addr.sin_addr);

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
            printf("[send_to_server] connect() failed: %lu (retries left: %d)\n", (unsigned long)WSAGetLastError(), retries);
            closesocket(sock);
            sock = INVALID_SOCKET;
            Sleep(500);
            retries--;
        }
    }

    if (sock == INVALID_SOCKET) {
        printf("[send_to_server] Error: failed to connect, err=%lu\n", (unsigned long)WSAGetLastError());
        fflush(stdout);
        WSACleanup();
        return -1;
    }

    printf("[send_to_server] Connected, sending command...\n");
    fflush(stdout);

    // Build TLV request
    size_t cmd_len = strlen(command);
    size_t total_size = cmd_len + 1;
    for (size_t i = 0; i < arg_count; i++) {
        if (args[i]) {
            total_size += snprintf(NULL, 0, "%zu", strlen(args[i])) + 1;
            total_size += strlen(args[i]) + 1;
        }
    }
    total_size += 1; // empty line

    char* send_buf = malloc(total_size);
    if (!send_buf) {
        closesocket(sock);
        WSACleanup();
        return -1;
    }

    char* sp = send_buf;
    memcpy(sp, command, cmd_len);
    sp += cmd_len;
    *sp++ = '\n';

    for (size_t i = 0; i < arg_count; i++) {
        if (args[i]) {
            size_t arg_len = strlen(args[i]);
            int len = snprintf(sp, total_size - (sp - send_buf), "%zu", arg_len);
            sp += len;
            *sp++ = '\n';
            memcpy(sp, args[i], arg_len);
            sp += arg_len;
            *sp++ = '\n';
        }
    }
    *sp++ = '\n';

    int sent = send(sock, send_buf, (int)(sp - send_buf), 0);
    free(send_buf);
    if (sent <= 0) {
        printf("[send_to_server] send() failed: %lu\n", (unsigned long)WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return -1;
    }

    // Read response: status\n[TLV data...\n]<empty line>
    char status_buf[32] = {0};
    char* status_out = status_buf;
    size_t status_remaining = sizeof(status_buf) - 1;
    while (status_remaining > 0) {
        char c;
        int n = recv(sock, &c, 1, 0);
        if (n <= 0) break;
        if (c == '\n') break;
        *status_out++ = c;
        status_remaining--;
    }
    *status_out = '\0';

    printf("[send_to_server] Response status: '%s'\n", status_buf);
    fflush(stdout);

    int result = -1;

    if (strcmp(status_buf, "OK") == 0) {
        char* out = out_buffer;
        size_t remaining = buffer_size - 1;
        int first = 1;

        while (1) {
            char len_line[32] = {0};
            char* lp = len_line;
            while (1) {
                char c;
                int n = recv(sock, &c, 1, 0);
                if (n <= 0) break;
                if (c == '\n') break;
                *lp++ = c;
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
            int n = recv(sock, &c, 1, 0);
            if (n <= 0) break;
            *out++ = c;
            count++;
            remaining--;
        }
        *out = '\0';

        // Consume trailing newline after TLV value
        {
            char nl;
            int n = recv(sock, &nl, 1, 0);
            if (n == 1 && nl == '\r') {
                recv(sock, &nl, 1, 0); // consume \n after \r
            }
        }
        }
        result = 0;
    } else if (strcmp(status_buf, "ERROR") == 0) {
        char len_line[32] = {0};
        char* lp = len_line;
        while (1) {
            char c;
            int n = recv(sock, &c, 1, 0);
            if (n <= 0) break;
            if (c == '\n') break;
            *lp++ = c;
        }

        if (strlen(len_line) > 0) {
            int expected_len = atoi(len_line);
            if (expected_len > 0 && expected_len < (int)buffer_size) {
                int bytes_read = 0;
                while (bytes_read < expected_len) {
                    int n = recv(sock, out_buffer + bytes_read, expected_len - bytes_read, 0);
                    if (n <= 0) break;
                    bytes_read += n;
                }
                out_buffer[bytes_read] = '\0';
            }
        }
        result = -1;
    }

    closesocket(sock);
    WSACleanup();
    return result;
}

int ServerRegister(const char* name) {
    if (!name) return -1;

    char local_ip[64] = {0};
    GetLocalIp(g_server_ip, local_ip, sizeof(local_ip));
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
    GetLocalIp(g_server_ip, local_ip, sizeof(local_ip));
    const char* args[2] = {name, local_ip};
    char response[256] = {0};
    int result = send_to_server_tlv("CONNECT", args, local_ip[0] ? 2 : 1, response, sizeof(response));
    if (result == 0 || strstr(response, "already connected") != NULL) {
        SetServerSessionActive(1);
        return 0;
    }
    if (response[0]) {
        LOGF("[ServerConnect] Server error: '%s'", response);
    } else {
        LOGF("[ServerConnect] TCP connection to %s:%d failed", g_server_ip, get_server_port());
    }
    return result;
}

int ServerDisconnect(const char* name) {
    if (!name) return -1;
    const char* args[1] = {name};
    char response[256];
    int result = send_to_server_tlv("DISCONNECT", args, 1, response, sizeof(response));
    if (result == 0) SetServerSessionActive(0);
    return result;
}

int ServerRefresh(const char* name) {
    if (!name) return -1;
    const char* args[1] = {name};
    char response[256];
    int result = send_to_server_tlv("REFRESH", args, 1, response, sizeof(response));
    if (result == 0) SetServerSessionActive(1);
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

// --- Event-driven server subscription helpers ---

// Buffered line reader used by server_subscribe_loop
typedef struct {
    SOCKET sock;
    char buf[4096];
    size_t pos;
    size_t len;
} SrvLineReader;

static void srv_line_reader_init(SrvLineReader* r, SOCKET sock) {
    r->sock = sock;
    r->pos = 0;
    r->len = 0;
}

// Read a single LF-terminated line (strips CR). Returns 1 on success,
// 2 on recv timeout, 0 on error/disconnect.
static int srv_readline(SrvLineReader* r, char* out, size_t out_size) {
    size_t consumed = 0;
    while (consumed + 1 < out_size) {
        if (r->pos >= r->len) {
            r->pos = 0;
            r->len = 0;
            int n = recv(r->sock, r->buf, sizeof(r->buf) - 1, 0);
            if (n <= 0) {
                if (n == 0) return 0; // connection closed
                if (WSAGetLastError() == WSAETIMEDOUT) return 2; // recv timeout
                return 0; // real error
            }
            r->len = (size_t)n;
        }
        char c = r->buf[r->pos++];
        if (c == '\n') {
            out[consumed] = '\0';
            return 1;
        }
        if (c != '\r') {
            out[consumed++] = c;
        }
    }
    out[consumed] = '\0';
    return 1;
}

// Read a TLV value (length line + data line) via line reader.
static int srv_read_tlv(SrvLineReader* r, char* out, size_t out_size) {
    char len_str[32];
    if (srv_readline(r, len_str, sizeof(len_str)) != 1) return 0;
    int expected = atoi(len_str);
    if (expected < 0 || (size_t)expected >= out_size) return 0;
    // Read exactly expected bytes
    size_t got = 0;
    while (got < (size_t)expected) {
        if (r->pos >= r->len) {
            int n = recv(r->sock, r->buf, sizeof(r->buf) - 1, 0);
            if (n <= 0) return 0;
            r->pos = 0;
            r->len = (size_t)n;
        }
        size_t avail = r->len - r->pos;
        size_t want = (size_t)expected - got;
        size_t copy = avail < want ? avail : want;
        memcpy(out + got, r->buf + r->pos, copy);
        r->pos += copy;
        got += copy;
    }
    out[got] = '\0';
    // Consume trailing newline
    if (r->pos >= r->len) {
        int n = recv(r->sock, r->buf, sizeof(r->buf) - 1, 0);
        if (n <= 0) return 0;
        r->pos = 0;
        r->len = (size_t)n;
    }
    if (r->buf[r->pos] == '\r') r->pos++;
    if (r->pos < r->len && r->buf[r->pos] == '\n') r->pos++;
    return 1;
}

// Fetches and applies the whitelist from the server.
static void sync_whitelist_from_server(const char* username) {
    char whitelist_buf[DAEMON_MAX_MESSAGE_SIZE];
    const char* wl_args[1] = {username};
    char response[8192] = {0};

    if (send_to_server_tlv("GET_WHITELIST", wl_args, 1, response, sizeof(response)) != 0) {
        LOGF("[Daemon] Failed to fetch whitelist from server");
        return;
    }

    // Server whitelist sync disabled
    if (strcmp(response, "SYNC_DISABLED") == 0) {
        if (strcmp(g_last_whitelist, "SYNC_DISABLED") != 0) {
            if (g_fallback_whitelist_enabled) {
                LOGF("[Daemon] Server whitelist sync is disabled, keeping local whitelist");
            } else {
                LOGF("[Daemon] Server whitelist sync is disabled, fallback off - blocking all traffic");
                if (DaemonSetWhitelist("", 0, whitelist_buf, sizeof(whitelist_buf)) == 0) {
                    g_whitelist_revision++;
                    g_blocked_all_pushed = 1;
                    NotifyStateChanged();
                }
            }
            strncpy(g_last_whitelist, "SYNC_DISABLED", sizeof(g_last_whitelist) - 1);
            g_last_whitelist[sizeof(g_last_whitelist) - 1] = '\0';
        }
        return;
    }

    if (g_blocked_all_pushed) {
        g_blocked_all_pushed = 0;
        g_last_whitelist[0] = '\0';
    }
    if (strcmp(response, g_last_whitelist) == 0) {
        return;
    }
    size_t copy_len = strlen(response);
    if (copy_len >= sizeof(g_last_whitelist)) copy_len = sizeof(g_last_whitelist) - 1;
    memcpy(g_last_whitelist, response, copy_len);
    g_last_whitelist[copy_len] = '\0';

    if (DaemonSetWhitelist(response, strlen(response), whitelist_buf, sizeof(whitelist_buf)) != 0) {
        LOGF("[Daemon] Failed to set whitelist on daemon");
    } else {
        LOGF("[Daemon] Whitelist updated");
        g_whitelist_revision++;
        NotifyStateChanged();
    }
}

// Syncs filtration state from the server.
static void sync_filtration_from_server(void) {
    char filt_buf[16] = {0};
    if (send_to_server_tlv("GET_FILTRATION", NULL, 0, filt_buf, sizeof(filt_buf)) != 0) return;
    int server_filt = (filt_buf[0] == '1');
    if (server_filt != g_filtration_enabled) {
        LOGF("[Daemon] Server filtration %s, updating local firewall", server_filt ? "enabled" : "disabled");
        DaemonSetFiltration(server_filt);
    }
}

// Blocks all traffic if the server is unreachable and fallback is disabled.
static void block_all_if_no_fallback(void) {
    if (g_fallback_whitelist_enabled || g_blocked_all_pushed) return;
    char whitelist_buf[DAEMON_MAX_MESSAGE_SIZE];
    LOGF("[Daemon] Server unreachable and fallback off - blocking all traffic");
    if (DaemonSetWhitelist("", 0, whitelist_buf, sizeof(whitelist_buf)) == 0) {
        g_whitelist_revision++;
        g_blocked_all_pushed = 1;
        NotifyStateChanged();
    }
}

// Tries to rediscover the configured server. Returns 1 if found.
static int try_discover_server(void) {
    char expected_name[128] = {0};
    ini_get_string("server", "name", expected_name, sizeof(expected_name));
    if (expected_name[0] == '\0') return 0;

    char local_ip[64] = {0}, bcast_list[4096] = {0};
    GetAllBroadcastAddresses(local_ip, sizeof(local_ip), bcast_list, sizeof(bcast_list));
    if (bcast_list[0] == '\0') return 0;

    char disco_resp[8192] = {0};
    int found = DiscoverServers(bcast_list, 42069, 2000, disco_resp, sizeof(disco_resp));
    if (found <= 0) return 0;

    char* line = disco_resp;
    for (int i = 0; i < found && line && *line; i++) {
        char* first_pipe = strchr(line, '|');
        if (!first_pipe) { char* nl = strchr(line, '\n'); if (nl) line = nl + 1; else break; continue; }
        *first_pipe = '\0';
        char* name = line;
        char* rest = first_pipe + 1;
        if (strcmp(name, expected_name) == 0) {
            char* second_pipe = strchr(rest, '|');
            char* server_ip = rest;
            char* server_port = "1984";
            if (second_pipe) {
                *second_pipe = '\0';
                server_port = second_pipe + 1;
                char* nl = strchr(server_port, '\n');
                if (nl) *nl = '\0';
            } else {
                char* nl = strchr(server_ip, '\n');
                if (nl) *nl = '\0';
            }
            if (local_ip[0] != '\0' && strcmp(server_ip, local_ip) == 0) {
                SetServerIp(".");
            } else {
                SetServerIp(server_ip);
            }
            ini_set_string("server", "port", server_port);
            LOGF("[Daemon] Server '%s' found at %s:%s", expected_name, server_ip, server_port);
            *first_pipe = '|';
            return 1;
        }
        *first_pipe = '|';
        char* nl = strchr(line, '\n');
        if (nl) line = nl + 1; else break;
    }
    return 0;
}

// Handle a server-pushed event. Reads the event's data TLV lines (terminated
// by an empty line) and applies the change. Returns 0 to continue, 1 to resubscribe.
static int handle_server_event(const char* event_type, SrvLineReader* reader, const char* username) {
    // Read data key=value lines until empty line
    char data[8][256];
    int data_count = 0;
    while (data_count < 8) {
        char line[256];
        if (srv_readline(reader, line, sizeof(line)) != 1) return 0;
        if (line[0] == '\0') break; // empty line terminates event
        strncpy(data[data_count], line, sizeof(data[data_count]) - 1);
        data[data_count][sizeof(data[data_count]) - 1] = '\0';
        data_count++;
    }

    if (strcmp(event_type, "WHITELIST_CHANGED") == 0) {
        sync_whitelist_from_server(username);
        return 0;
    }
    if (strcmp(event_type, "FILTRATION_TOGGLED") == 0) {
        for (int i = 0; i < data_count; i++) {
            char* eq = strchr(data[i], '=');
            if (eq && strncmp(data[i], "enabled", (size_t)(eq - data[i])) == 0) {
                int val = (*(eq + 1) == '1');
                if (val != g_filtration_enabled) {
                    LOGF("[Daemon] Server filtration toggled to %d, updating local firewall", val);
                    DaemonSetFiltration(val);
                }
            }
        }
        return 0;
    }
    if (strcmp(event_type, "USER_APPROVED") == 0) {
        for (int i = 0; i < data_count; i++) {
            char* eq = strchr(data[i], '=');
            if (eq && strncmp(data[i], "name", (size_t)(eq - data[i])) == 0) {
                const char* approved_name = eq + 1;
                if (strcmp(approved_name, username) == 0) {
                    LOGF("[Daemon] User '%s' approved, connecting to server...", approved_name);
                    if (ServerConnect(username) == 0) {
                        SetServerSessionActive(1);
                        apply_auto_enable();
                        sync_whitelist_from_server(username);
                        sync_filtration_from_server();
                    }
                }
            }
        }
        return 0;
    }
    // Unknown event type — data already consumed above
    return 0;
}

// Runs the server subscription loop. Returns 0 on connection loss, 1 on service stop.
// Blocks until the connection drops or the service should shut down.
static int server_subscribe_loop(const char* username, int* consecutive_failures) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;

    const char* target_ip = g_server_ip;
    if (strcmp(g_server_ip, ".") == 0 || strcmp(g_server_ip, "127.0.0.1") == 0) {
        target_ip = "127.0.0.1";
    }
    int port = get_server_port();

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) { WSACleanup(); return 0; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    inet_pton(AF_INET, target_ip, &addr.sin_addr);

    // Set receive timeout for event parsing
    int timeout_ms = 3000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(sock);
        WSACleanup();
        if (consecutive_failures) (*consecutive_failures)++;
        return 0;
    }

    // Send SUBSCRIBE with username
    char sub_req[1024];
    int sub_len = snprintf(sub_req, sizeof(sub_req), "SUBSCRIBE\n%zu\n%s\n\n",
                           strlen(username), username);
    if (send(sock, sub_req, sub_len, 0) <= 0) {
        closesocket(sock);
        WSACleanup();
        return 0;
    }

    // Read OK response
    SrvLineReader reader;
    srv_line_reader_init(&reader, sock);
    char status[32];
    if (!srv_readline(&reader, status, sizeof(status)) || strcmp(status, "OK") != 0) {
        LOGF("[Daemon] SUBSCRIBE handshake failed: '%s'", status);
        closesocket(sock);
        WSACleanup();
        return 0;
    }
    // Consume empty line terminator
    char empty_line[4];
    srv_readline(&reader, empty_line, sizeof(empty_line));

    LOGF("[Daemon] Subscribed to server events for user '%s'", username);

    // On successful subscribe, ensure we are connected and synced
    if (ServerConnect(username) == 0) {
        SetServerSessionActive(1);
        apply_auto_enable();
    } else {
        apply_auto_disable();
    }
    sync_whitelist_from_server(username);
    sync_filtration_from_server();
    if (consecutive_failures) *consecutive_failures = 0;

    // Event loop
    DWORD last_heartbeat = GetTickCount();
    DWORD last_local_check = GetTickCount();
    int keep_going = 1;

    while (keep_going) {
        // Check for service stop
        if (g_ServiceStopEvent != INVALID_HANDLE_VALUE &&
            WaitForSingleObject(g_ServiceStopEvent, 0) == WAIT_OBJECT_0) {
            keep_going = 0;
            break;
        }

        // Heartbeat every SRV_HEARTBEAT_INTERVAL_MS
        DWORD now = GetTickCount();
        if (now - last_heartbeat >= SRV_HEARTBEAT_INTERVAL_MS) {
            last_heartbeat = now;
            if (send(sock, "K\n", 2, 0) <= 0) {
                LOGF("[Daemon] Heartbeat send failed, connection lost");
                break;
            }
        }

        // Check local daemon health
        if (now - last_local_check >= SRV_LOCAL_DAEMON_CHECK_MS) {
            last_local_check = now;
            PingDaemon(); // just check — failure is non-fatal here
        }

        // Try to read a line (non-blocking with SO_RCVTIMEO)
        char line[512];
        int rd = srv_readline(&reader, line, sizeof(line));
        if (rd == 0) {
            // Connection closed or real error — leave loop
            keep_going = 0;
            break;
        }
        if (rd == 2) {
            // recv timeout — loop back to check heartbeat/stop
            continue;
        }

        if (strcmp(line, "EVENT") == 0) {
            char event_type[64];
            if (!srv_read_tlv(&reader, event_type, sizeof(event_type))) {
                break; // bad data
            }

            if (handle_server_event(event_type, &reader, username) == 1) {
                // Resubscribe requested
                break;
            }
        } else if (strcmp(line, "K") == 0) {
            // Server heartbeat reply — ignore
        } else {
            // Unknown line — skip rest of event if any
            while (1) {
                char tmp[256];
                if (!srv_readline(&reader, tmp, sizeof(tmp))) {
                    keep_going = 0;
                    break;
                }
                if (tmp[0] == '\0') break;
            }
        }
    }

    closesocket(sock);
    WSACleanup();
    return keep_going; // 1 = service stop, 0 = connection lost
}

// Sleep in increments of 1 second, checking for service stop.
// Returns 1 if stop was requested, 0 if slept the full duration.
static int sleep_with_stop_check(int total_ms) {
    int slept = 0;
    while (slept < total_ms) {
        if (g_ServiceStopEvent != INVALID_HANDLE_VALUE) {
            if (WaitForSingleObject(g_ServiceStopEvent, 0) == WAIT_OBJECT_0) {
                return 1;
            }
        }
        Sleep(1000);
        slept += 1000;
    }
    return 0;
}

int DaemonRun(const char* server_ip) {
    char username[128] = {0};
    int consecutive_failures = 0;
    int startup_retries = 30;

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
    }

    // Try to connect to server on startup
    if (LoadUserName(username, sizeof(username)) == 0 && username[0] != '\0') {
        LOGF("[Daemon] Found saved username: %s, connecting to server...", username);
        if (ServerConnect(username) == 0) {
            LOGF("[Daemon] Connected to server successfully");
            SetServerSessionActive(1);
            apply_auto_enable();
        } else {
            LOGF("[Daemon] Failed to connect to server (may not be registered/approved yet)");
            apply_auto_disable();
            if (!g_registration_tried) {
                LOGF("[Daemon] Attempting to register...");
                ServerRegister(username);
                g_registration_tried = 1;
            }
        }
    } else {
        LOGF("[Daemon] No saved username found, skipping server connection");
        apply_auto_disable();
    }

    // Event-driven subscription loop
    while (1) {
        // Re-read username from config (may have been set via frontend)
        username[0] = '\0';
        LoadUserName(username, sizeof(username));

        // Check for stop event
        if (g_ServiceStopEvent != INVALID_HANDLE_VALUE) {
            if (WaitForSingleObject(g_ServiceStopEvent, 0) == WAIT_OBJECT_0) {
                LOGF("[Daemon] Stop signal received, exiting...");
                break;
            }
        }

        if (local_ready && PingDaemon() != 0) {
            LOGF("[Daemon] Local daemon not responding, continuing in degraded mode");
            local_ready = 0;
        }

        if (username[0] == '\0') {
            // No username yet — sleep and retry
            sleep_with_stop_check(5000);
            continue;
        }

        // Try to subscribe to server events
        int sub_result = server_subscribe_loop(username, &consecutive_failures);
        if (sub_result == 1) {
            // Service stop requested
            break;
        }

        // Connection lost — clean up state
        LOGF("[Daemon] Lost connection to server %s, retrying...", g_server_ip);
        SetServerSessionActive(0);
        apply_auto_disable();
        block_all_if_no_fallback();

        consecutive_failures++;
        if (consecutive_failures >= 3) {
            if (try_discover_server()) {
                consecutive_failures = 0;
            } else {
                LOGF("[Daemon] Server discovery failed");
            }
        }

        if (!g_registration_tried) {
            LOGF("[Daemon] Attempting to register...");
            ServerRegister(username);
            g_registration_tried = 1;
        }

        // Wait before retrying
        sleep_with_stop_check(5000);
    }

    return 0;
}
