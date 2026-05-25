/**
 * @file ipc_daemon.c
 * @brief IPC client implementation for connecting to BigBrother Daemon.
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include "../include/ipc_daemon.h"
#include "../../../common/log/log.h"
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define DAEMON_PIPE_PREFIX "\\\\.\\pipe\\"
#define CONFIG_INI_PATH "config\\config.ini"
#define INI_LINE_MAX 512

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

// Forward declare get_local_ip (defined later in this file)
static int get_local_ip(const char* server_ip, char* buffer, size_t buffer_size);

#define DISCOVERY_PORT 42069
#define DISCOVERY_MAGIC "BIGBROTHER_DISCOVERY"
#define DISCOVERY_RESPONSE_MAGIC "BIGBROTHER_DISCOVERY_RESPONSE"
#define DISCOVERY_TIMEOUT_MS 2000
#define DISCOVERY_BUF_SIZE 4096

// Auto-detect local IP address and subnet mask using Windows IP helper API.
// Returns 0 on success with ip_str, mask, and broadcast_str filled.
// Get all active broadcast addresses, one per line in bcast_out.
// Also returns the first non-loopback IP in ip_str.
// Returns number of broadcast addresses found, or 0 on failure.
int GetAllBroadcastAddresses(char* ip_str, size_t ip_size, char* bcast_out, size_t bcast_size) {
    if (ip_str) ip_str[0] = '\0';
    if (bcast_out) bcast_out[0] = '\0';

    ULONG buf_len = 0;
    GetAdaptersAddresses(AF_INET, 0, NULL, NULL, &buf_len);
    if (buf_len == 0) return 0;

    IP_ADAPTER_ADDRESSES* adapters = (IP_ADAPTER_ADDRESSES*)malloc(buf_len);
    if (!adapters) return 0;

    ULONG ret = GetAdaptersAddresses(AF_INET, 0, NULL, adapters, &buf_len);
    if (ret != NO_ERROR) {
        free(adapters);
        return 0;
    }

    int count = 0;
    char* out = bcast_out;
    size_t remaining = bcast_size;
    int first_ip_set = 0;

    for (IP_ADAPTER_ADDRESSES* a = adapters; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;
        if (a->FirstUnicastAddress == NULL) continue;

        SOCKET_ADDRESS* saddr = &a->FirstUnicastAddress->Address;
        struct sockaddr_in* sin = (struct sockaddr_in*)saddr->lpSockaddr;
        if (!sin) continue;

        ULONG ip = ntohl(sin->sin_addr.s_addr);
        if (ip == 0x7f000001) continue; // skip loopback

        // Save first IP
        if (!first_ip_set && ip_str) {
            snprintf(ip_str, ip_size, "%lu.%lu.%lu.%lu",
                (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
            first_ip_set = 1;
        }

        // Compute broadcast: ip | ~mask
        ULONG prefix = a->FirstUnicastAddress->OnLinkPrefixLength;
        ULONG mask_val = prefix ? (0xFFFFFFFF << (32 - prefix)) : 0x00FFFFFF;
        ULONG bcast = ip | ~mask_val;

        if (out && remaining > 0) {
            int written = snprintf(out, remaining, "%lu.%lu.%lu.%lu\n",
                (bcast >> 24) & 0xFF, (bcast >> 16) & 0xFF, (bcast >> 8) & 0xFF, bcast & 0xFF);
            if (written > 0 && written < (int)remaining) {
                out += written;
                remaining -= written;
                count++;
            }
        }
    }

    free(adapters);

    // Fallback: if no adapters found, try get_local_ip and assume /24
    if (count == 0) {
        char local_ip[64] = {0};
        get_local_ip("8.8.8.8", local_ip, sizeof(local_ip));
        if (local_ip[0]) {
            if (ip_str) snprintf(ip_str, ip_size, "%s", local_ip);
            unsigned int a, b, c, d;
            if (sscanf(local_ip, "%u.%u.%u.%u", &a, &b, &c, &d) == 4 && bcast_out && bcast_size > 0) {
                snprintf(bcast_out, bcast_size, "%u.%u.%u.255\n", a, b, c);
                count = 1;
            }
        }
    }

    return count;
}

// UDP broadcast: send discovery magic to all broadcast addresses, collect all server responses.
// bcast_list is a newline-separated list of broadcast addresses (e.g. "192.168.1.255\n10.0.0.255\n").
// Returns number of servers found (written to out as "name|ip\n..." lines).
int DiscoverServers(const char* bcast_list, int port, int timeout_ms, char* out, size_t out_size) {
    if (!out || out_size == 0) return 0;
    out[0] = '\0';

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return 0;
    }

    // Enable broadcast
    int bcast_opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char*)&bcast_opt, sizeof(bcast_opt));

    // Set receive timeout
    DWORD rcv_timeout = timeout_ms > 0 ? timeout_ms : DISCOVERY_TIMEOUT_MS;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&rcv_timeout, sizeof(rcv_timeout));

    // Bind to any port
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = 0;
    bind(sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr));

    char request[128];
    snprintf(request, sizeof(request), "%s\n1\n", DISCOVERY_MAGIC);

    // Send discovery broadcast to each address in the list
    {
        char list_copy[512] = {0};
        strncpy(list_copy, bcast_list ? bcast_list : "127.0.0.1", sizeof(list_copy) - 1);

        struct sockaddr_in dest;
        memset(&dest, 0, sizeof(dest));
        dest.sin_family = AF_INET;
        dest.sin_port = htons((short)port);

        char* line = list_copy;
        while (line && *line) {
            char* nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            if (line[0] != '\0') {
                inet_pton(AF_INET, line, &dest.sin_addr);
                sendto(sock, request, (int)strlen(request), 0, (struct sockaddr*)&dest, sizeof(dest));
            }
            if (nl) line = nl + 1; else break;
        }
    }

    // Also send directly to localhost (for same-machine discovery)
    {
        struct sockaddr_in dest;
        memset(&dest, 0, sizeof(dest));
        dest.sin_family = AF_INET;
        dest.sin_port = htons((short)port);
        inet_pton(AF_INET, "127.0.0.1", &dest.sin_addr);
        sendto(sock, request, (int)strlen(request), 0, (struct sockaddr*)&dest, sizeof(dest));
    }

    // Collect responses
    int count = 0;
    char* out_pos = out;
    size_t remaining = out_size - 1;

    while (count < DISCOVERY_MAX_SERVERS) {
        struct sockaddr_in from;
        int from_len = sizeof(from);
        char buf[DISCOVERY_BUF_SIZE];
        int n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&from, &from_len);
        if (n <= 0) break; // timeout or error

        buf[n] = '\0';

        // Parse response: "BIGBROTHER_DISCOVERY_RESPONSE\n<name>\n<ip>\n"
        char* line = buf;
        if (strncmp(line, DISCOVERY_RESPONSE_MAGIC, strlen(DISCOVERY_RESPONSE_MAGIC)) != 0) continue;

        // Skip magic line
        line = strchr(line, '\n');
        if (!line) continue;
        line++;

        // Server name
        char* name = line;
        char* nl = strchr(line, '\n');
        if (!nl) continue;
        *nl = '\0';
        line = nl + 1;

        // Server IP
        char* ip = line;
        nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        // Deduplicate by IP: skip if we already have this server
        int dup = 0;
        char* check = out;
        while (check && check < out_pos) {
            char* pipe = strchr(check, '|');
            if (pipe) {
                char* check_ip = pipe + 1;
                char* nl = strchr(check_ip, '\n');
                if (nl) *nl = '\0';
                if (strcmp(check_ip, ip) == 0) { dup = 1; if (nl) *nl = '\n'; break; }
                if (nl) *nl = '\n';
            }
            char* next_nl = strchr(check, '\n');
            check = next_nl ? next_nl + 1 : NULL;
        }
        if (dup) continue;

        int written = snprintf(out_pos, remaining, "%s|%s\n", name, ip);
        if (written > 0 && written < (int)remaining) {
            out_pos += written;
            remaining -= written;
            count++;
        } else {
            break;
        }
    }

    closesocket(sock);
    WSACleanup();
    return count;
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

#define SERVER_TCP_PORT 42070

static int send_to_server_tlv(const char* command, const char** args, size_t arg_count,
                              char* out_buffer, size_t buffer_size) {
    if (!g_server_ip[0] || !command || !out_buffer || buffer_size == 0) {
        printf("[send_to_server] Error: invalid parameters or empty server IP\n");
        return -1;
    }

    // Resolve target IP: "." or "127.0.0.1" means localhost
    const char* target_ip = g_server_ip;
    if (strcmp(g_server_ip, ".") == 0 || strcmp(g_server_ip, "127.0.0.1") == 0) {
        target_ip = "127.0.0.1";
    }

    printf("[send_to_server] Connecting to %s:%d...\n", target_ip, SERVER_TCP_PORT);
    fflush(stdout);

    // TCP socket
    SOCKET sock = INVALID_SOCKET;
    int retries = 10;

    while (retries > 0 && sock == INVALID_SOCKET) {
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            printf("[send_to_server] socket() failed: %lu\n", (unsigned long)WSAGetLastError());
            return -1;
        }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(SERVER_TCP_PORT);
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
    int consecutive_failures = 0;

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
        // Re-read username from config on each iteration (may have been set via frontend)
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
            LOGF("[Daemon] Local daemon not responding (pid: %lu), continuing in degraded mode", GetCurrentProcessId());
            local_ready = 0;
        }

        // Refresh connection periodically
        if (server_connected && username[0] != '\0') {
            ServerRefresh(username);
        }

        // Always try to get whitelist from remote server (detects active WL changes)
        if (username[0] != '\0') {
            const char* wl_args[1] = {username};
            char response[8192] = {0};
            if (send_to_server_tlv("GET_WHITELIST", wl_args, 1, response, sizeof(response)) == 0) {
                consecutive_failures = 0;
                if (!server_connected) {
                    server_connected = 1;
                    g_server_session_active = 1;
                    ServerConnect(username);
                }

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

                // Server returned actual whitelist entries - push if changed
                if (blocked_all_pushed) {
                    blocked_all_pushed = 0;
                    last_whitelist[0] = '\0';
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
                if (server_connected) {
                    LOGF("[Daemon] Lost connection to server %s", g_server_ip);
                } else {
                    LOGF("[Daemon] Cannot connect to server %s, retrying...", g_server_ip);
                }
                server_connected = 0;
                g_server_session_active = 0;

                consecutive_failures++;
                if (consecutive_failures >= 3) {
                    LOGF("[Daemon] %d consecutive failures, trying server discovery...", consecutive_failures);
                    char expected_name[128] = {0};
                    ini_get_string("server", "name", expected_name, sizeof(expected_name));
                    if (expected_name[0] != '\0') {
                        char local_ip[64] = {0}, bcast_list[512] = {0};
                        GetAllBroadcastAddresses(local_ip, sizeof(local_ip), bcast_list, sizeof(bcast_list));
                        if (bcast_list[0] != '\0') {
                            char disco_resp[8192] = {0};
                            int found = DiscoverServers(bcast_list, 42069, 2000, disco_resp, sizeof(disco_resp));
                            if (found > 0) {
                                char* line = disco_resp;
                                for (int i = 0; i < found && line && *line; i++) {
                                    char* pipe = strchr(line, '|');
                                    if (pipe) {
                                        *pipe = '\0';
                                        if (strcmp(line, expected_name) == 0) {
                                            char* server_ip = pipe + 1;
                                            // If server is on the same machine, use "." (localhost TCP)
                                            if (local_ip[0] != '\0' && strcmp(server_ip, local_ip) == 0) {
                                                SetServerIp(".");
                                            } else {
                                                SetServerIp(server_ip);
                                            }
                                            LOGF("[Daemon] Server '%s' found at new IP: %s", expected_name, server_ip);
                                            consecutive_failures = 0;
                                            break;
                                        }
                                        *pipe = '|';
                                    }
                                    char* nl = strchr(line, '\n');
                                    if (nl) line = nl + 1; else break;
                                }
                            }
                        }
                    }
                }

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
