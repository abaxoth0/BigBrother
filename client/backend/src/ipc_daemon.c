/**
 * @file ipc_daemon.c
 * @brief IPC client implementation for connecting to BigBrother Daemon.
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include "../include/ipc_daemon.h"
#include "../include/ipc_client.h"
#include "../include/tls_client.h"
#include "../include/client_key.h"
#include "../include/certificate_pin.h"
#include "../../../common/log/log.h"
#include "../../../common/file_security.h"
#include "notify.h"
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define DAEMON_PIPE_PREFIX "\\\\.\\pipe\\"
#define CONFIG_INI_PATH "config\\config.ini"
#define INI_LINE_MAX 512

#define SRV_HEARTBEAT_INTERVAL_MS 10000
#define SRV_LOCAL_DAEMON_CHECK_MS 10000
#define SERVER_INVALID_TOKEN (-2)

// Serializes config.ini access since handlers run on multiple threads
// and ini_set_string rewrites the file (non-atomic without the lock).
static CRITICAL_SECTION g_config_lock;
static INIT_ONCE g_config_once = INIT_ONCE_STATIC_INIT;
static volatile LONG g_certificate_revision = 0;

static BOOL CALLBACK initialize_config_lock(PINIT_ONCE once, PVOID param, PVOID* context) {
    (void)once; (void)param; (void)context;
    InitializeCriticalSection(&g_config_lock);
    return TRUE;
}

static void config_lock_init(void) {
    InitOnceExecuteOnce(&g_config_once, initialize_config_lock, NULL, NULL);
}

static int ini_get_string_locked(const char* section, const char* key, char* out, size_t out_size);
static int ini_set_string_locked(const char* section, const char* key, const char* value);

static char g_server_ip[64] = {0};
static volatile LONG g_server_session_active = 0;
static int g_registration_tried = 0;
static int g_fallback_whitelist_enabled = 1;
// Desired filtration state (set explicitly by the user via the "Вкл/Выкл"
// button, or by the server). The effective state applied to the firewall also
// depends on the server connection when connection-sync is enabled.
static int g_filtration_requested = 1;
// Effective filtration state last pushed to the firewall.
static int g_filtration_enabled = 1;

static int apply_filtration_sync(void);

// Last whitelist pushed to the firewall (heap-allocated, grows on demand so a
// large server whitelist is not truncated at a fixed size).
static char* g_last_whitelist = NULL;
static size_t g_last_whitelist_cap = 0;
static int g_blocked_all_pushed = 0;

static int wl_last_reserve(size_t need) {
    if (need + 1 <= g_last_whitelist_cap) return 0;
    size_t cap = need + 1;
    if (cap < 4096) cap = 4096;
    char* nb = realloc(g_last_whitelist, cap);
    if (!nb) return -1;
    g_last_whitelist = nb;
    g_last_whitelist_cap = cap;
    return 0;
}

static void wl_last_set(const char* s) {
    size_t len = strlen(s);
    if (wl_last_reserve(len) == 0 && g_last_whitelist) {
        memcpy(g_last_whitelist, s, len + 1);
    }
}

static void wl_last_clear(void) {
    if (g_last_whitelist) g_last_whitelist[0] = '\0';
}

static int wl_last_equals(const char* s) {
    return g_last_whitelist && strcmp(g_last_whitelist, s) == 0;
}

void SetServerSessionActive(int active) {
    active = active ? 1 : 0;
    if (InterlockedExchange(&g_server_session_active, active) != active) {
        if (bb_notify("Соединение с сервером", active ? "Подключено к серверу" : "Соединение с сервером потеряно") != 0)
            LOGF("[Notifications] Could not launch connection notification");
        NotifyStateChanged();
        // Sync filtration with the connection state so both connected and
        // disconnected transitions (manual or unexpected) take effect.
        apply_filtration_sync();
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

int PrepareClientSecretPermissions(void) {
    char directory[MAX_PATH], config[MAX_PATH], key[MAX_PATH];
    get_exe_path(directory, sizeof(directory));
    size_t length = strlen(directory);
    while (length && directory[length - 1] == '\\') directory[--length] = '\0';
    if (length <= 3) return -1;
    DWORD attributes = GetFileAttributesA(directory);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return -1;
    for (const char** name = (const char*[]){"WINDIR", "PROGRAMFILES", "PROGRAMDATA", "USERPROFILE", NULL}; *name; name++) {
        char root[MAX_PATH];
        if (GetEnvironmentVariableA(*name, root, sizeof(root)) && _stricmp(root, directory) == 0) return -1;
    }
    snprintf(key, sizeof(key), "%s\\client_sigkey.key", directory);
    DWORD key_attributes = GetFileAttributesA(key);
    if (key_attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) return -1;
    } else if (bb_file_acl(key, 0, 0) != 0) return -1;
    snprintf(config, sizeof(config), "%s\\config", directory);
    if (!CreateDirectoryA(config, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return -1;
    if (bb_private_tree(config) != 0) return -1;
    return bb_file_acl(directory, 1, 1);
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

    config_lock_init();
    EnterCriticalSection(&g_config_lock);
    int found = ini_get_string_locked(section, key, out, out_size);
    LeaveCriticalSection(&g_config_lock);
    return found;
}

static int ini_get_string_locked(const char* section, const char* key, char* out, size_t out_size) {
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
    int credential = (_stricmp(section, "server") == 0 && _stricmp(key, "fingerprint") == 0) ||
        (_stricmp(section, "client") == 0 && _stricmp(key, "token") == 0);
    if (!credential) return 0;

    // Old builds could append credentials to the final unrelated section.
    // Recover one unambiguous, valid value before normal TLS can fall back to TOFU.
    f = fopen(ini_path, "r");
    if (!f) return 0;
    char recovered[65] = {0};
    size_t expected = _stricmp(key, "fingerprint") == 0 ? 64 : 48;
    int conflict = 0, invalid = 0;
    while (fgets(line, sizeof(line), f)) {
        char* p = trim_ws(line);
        if (*p == ';' || *p == '#' || *p == '[') continue;
        char* eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        if (_stricmp(trim_ws(p), key) != 0) continue;
        char* value = trim_ws(eq + 1);
        if (strlen(value) != expected) { invalid = 1; continue; }
        int valid = 1;
        for (size_t i = 0; i < expected; i++)
            if (!isxdigit((unsigned char)value[i])) valid = 0;
        if (!valid) { invalid = 1; continue; }
        int same = _stricmp(key, "fingerprint") == 0
            ? _stricmp(recovered, value) == 0 : strcmp(recovered, value) == 0;
        if (recovered[0] && !same) conflict = 1;
        else snprintf(recovered, sizeof(recovered), "%s", value);
    }
    fclose(f);
    if (conflict || invalid) {
        // Preserve rejection instead of silently trusting a new certificate.
        snprintf(out, out_size, "%s", conflict ? "AMBIGUOUS_MISPLACED_CREDENTIAL" : "INVALID_MISPLACED_CREDENTIAL");
        return 1;
    }
    if (!recovered[0]) return 0;
    snprintf(out, out_size, "%s", recovered);
    ini_set_string_locked(section, key, recovered);
    return 1;
}

// Write or update a string value in config.ini.
// Retains all other sections and keys.
int ini_set_string(const char* section, const char* key, const char* value) {
    config_lock_init();
    EnterCriticalSection(&g_config_lock);
    int result = ini_set_string_locked(section, key, value);
    LeaveCriticalSection(&g_config_lock);
    return result;
}

static int ini_set_string_locked(const char* section, const char* key, const char* value) {
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
    int in_section = 0;
    int repair_credential = (_stricmp(section, "server") == 0 && _stricmp(key, "fingerprint") == 0) ||
        (_stricmp(section, "client") == 0 && _stricmp(key, "token") == 0);

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
            in_section = (_stricmp(p + 1, section) == 0);
            if (in_section) {
                section_idx = i;
                last_in_section = i;
            }
            continue;
        }

        char* eq = strchr(p, '=');
        if (eq) {
            *eq = '\0';
            char* k = trim_ws(p);
            if (_stricmp(k, key) == 0) {
                if (in_section) key_idx = i;
                else if (repair_credential) lines[i][0] = '\0';
            }
        }
        if (in_section) last_in_section = i;
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
                size_t n = strlen(lines[i]);
                if (n && lines[i][n - 1] != '\n') fputc('\n', out);
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

    // Preserve the original on a failed atomic replacement. Callers must know
    // persistence failed so an in-memory replacement remains authoritative.
    int result = 0;
    if (!MoveFileEx(tmp_path, ini_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFile(tmp_path);
        result = -1;
    }
    for (int i = 0; i < line_count; i++) free(lines[i]);
    free(lines);
    return result;
}

void load_server_ip(void) {
    char buf[64] = {0};
    if (ini_get_string("server", "address", buf, sizeof(buf)) && buf[0]) {
        strncpy(g_server_ip, buf, sizeof(g_server_ip) - 1);
        DLOGF("load_server_ip: loaded IP = '%s'", g_server_ip);
    } else {
        DLOGF("load_server_ip: no config or empty");
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
        config_lock_init();
        EnterCriticalSection(&g_config_lock);
        snprintf(g_server_ip, sizeof(g_server_ip), "%s", ip);
        ini_set_string_locked("server", "address", ip);
        LeaveCriticalSection(&g_config_lock);
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
    int result = send_command_tlv("SET_WHITELIST", args, 1, out_buffer, buffer_size);
    if (result == 0 && strcmp(out_buffer, "OK") == 0 &&
        bb_notify("Белый список", "Белый список обновлён") != 0)
        LOGF("[Notifications] Could not launch whitelist notification");
    if (result == 0 && strcmp(out_buffer, "OK") != 0) return -1;
    return result;
}

int DaemonReloadWhitelist(char* out_buffer, size_t buffer_size) {
    int result = send_command_tlv("RELOAD", NULL, 0, out_buffer, buffer_size);
    if (result == 0 && strcmp(out_buffer, "OK") == 0 &&
        bb_notify("Белый список", "Белый список перезагружен") != 0)
        LOGF("[Notifications] Could not launch whitelist notification");
    if (result == 0 && strcmp(out_buffer, "OK") != 0) return -1;
    return result;
}

int PingDaemon(void) {
    char buffer[256];
    return send_command_tlv("PING", NULL, 0, buffer, sizeof(buffer));
}

int DaemonGetLogPath(char* out_buffer, size_t buffer_size) {
    return send_command_tlv("GET_LOG_PATH", NULL, 0, out_buffer, buffer_size);
}

static volatile LONG g_notification_filtration = -1;

static void notify_filtration(int enabled) {
    LONG previous = InterlockedExchange(&g_notification_filtration, enabled);
    if (previous != -1 && previous != enabled &&
        bb_notify("Фильтрация", enabled ? "Фильтрация включена" : "Фильтрация выключена") != 0)
        LOGF("[Notifications] Could not launch filtration notification");
}

int DaemonGetFiltration(void) {
    char buf[16] = {0};
    if (send_command_tlv("GET_FILTRATION", NULL, 0, buf, sizeof(buf)) == 0) {
        g_filtration_enabled = (buf[0] == '1');
    } else {
        // Firewall is not running — nothing is being filtered, report as off.
        g_filtration_enabled = 0;
    }
    notify_filtration(g_filtration_enabled);
    return g_filtration_enabled;
}

// Push the already-gated effective state to the firewall and cache it.
static int daemon_set_filtration_effective(int effective) {
    const char* args[1] = {effective ? "1" : "0"};
    char buf[16] = {0};
    if (send_command_tlv("SET_FILTRATION", args, 1, buf, sizeof(buf)) == 0 && strcmp(buf, "OK") == 0) {
        g_filtration_enabled = effective;
        notify_filtration(effective);
        NotifyStateChanged();
        return 0;
    }
    return -1;
}

int DaemonSetFiltration(int enabled) {
    int desired = enabled ? 1 : 0;
    int rc = 0;
    if (desired != g_filtration_requested) {
        g_filtration_requested = desired;
        rc = apply_filtration_sync();
    }
    // Always refresh the GUI so it shows the true (possibly connection-gated)
    // state even when the request itself cannot take effect yet.
    NotifyStateChanged();
    return rc;
}

int IsFiltrationEnabled(void) {
    return g_filtration_enabled;
}

int IsIpv6BlockEnabled(void) {
    char buf[8] = {0};
    if (ini_get_string("filtration", "block_ipv6", buf, sizeof(buf)) && buf[0])
        return strcmp(buf, "0") != 0;
    return 1;
}

static SRWLOCK g_ipv6_sync_lock = SRWLOCK_INIT;

static int sync_ipv6_block_locked(void) {
    int desired = IsIpv6BlockEnabled();
    char buf[32] = {0};
    if (send_command_tlv("GET_IPV6_BLOCK", NULL, 0, buf, sizeof(buf)) != 0)
        return -1;
    if (strcmp(buf, desired ? "1" : "0") == 0) return 0;
    const char* args[] = {desired ? "1" : "0"};
    if (send_command_tlv("SET_IPV6_BLOCK", args, 1, buf, sizeof(buf)) != 0)
        return -1;
    return strcmp(buf, "OK") == 0 ? 0 : -1;
}

static int sync_ipv6_block(void) {
    AcquireSRWLockExclusive(&g_ipv6_sync_lock);
    int result = sync_ipv6_block_locked();
    ReleaseSRWLockExclusive(&g_ipv6_sync_lock);
    return result;
}

int SetIpv6BlockEnabled(int enabled) {
    AcquireSRWLockExclusive(&g_ipv6_sync_lock);
    if (ini_set_string("filtration", "block_ipv6", enabled ? "1" : "0") != 0) {
        ReleaseSRWLockExclusive(&g_ipv6_sync_lock);
        return -1;
    }
    // Persist even while the firewall is stopped; startup/status restores it.
    int synced = sync_ipv6_block_locked();
    ReleaseSRWLockExclusive(&g_ipv6_sync_lock);
    if (synced != 0)
        LOGF("[Daemon] IPv6 blocking preference saved; firewall sync pending");
    return 0;
}

int IsFiltrationAutoDisableEnabled(void) {
    char buf[8] = {0};
    if (ini_get_string("filtration", "auto_disable", buf, sizeof(buf)) && buf[0])
        return buf[0] == '1';
    return 1; // enabled by default
}

void SetFiltrationAutoDisableEnabled(int enabled) {
    ini_set_string("filtration", "auto_disable", enabled ? "1" : "0");
    apply_filtration_sync();
}

// Recompute the effective filtration state. With connection-sync enabled,
// filtration is only effective while connected to the server; the user's
// explicit choice (g_filtration_requested) is never overridden.
static int apply_filtration_sync(void) {
    int effective = (IsFiltrationAutoDisableEnabled() && !IsServerSessionActive())
                        ? 0 : g_filtration_requested;
    if (effective != g_filtration_enabled) {
        LOGF("[Daemon] Filtration sync: requested=%d connected=%d -> effective=%d",
             g_filtration_requested, IsServerSessionActive(), effective);
        return daemon_set_filtration_effective(effective);
    }
    return 0;
}

// Public wrapper used by the IPC layer when reporting status: recomputes the
// connection-gated effective state (pushing it to the firewall if it changed),
// re-reads the firewall, and re-aligns it if it drifted (e.g. the firewall
// restarted with its default state). Returns the effective state to report —
// always 0 when the firewall is not running.
int RefreshFiltrationEffective(void) {
    sync_ipv6_block();
    apply_filtration_sync();
    int actual = DaemonGetFiltration();
    int effective = (IsFiltrationAutoDisableEnabled() && !IsServerSessionActive())
                        ? 0 : g_filtration_requested;
    if (actual != effective) {
        if (daemon_set_filtration_effective(effective) == 0) {
            return effective;
        }
        g_filtration_enabled = 0; // push failed -> firewall unreachable
        return 0;
    }
    return actual;
}

static int tls_connect_socket(SOCKET sock, bb_tls_t* tls);
static void get_client_token(char* buf, size_t size);

static int send_to_server_tlv(const char* command, const char** args, size_t arg_count,
                              char* out_buffer, size_t buffer_size) {
    if (!g_server_ip[0] || !command || !out_buffer || buffer_size == 0) {
        printf("[send_to_server] Error: invalid parameters or empty server IP\n");
        return -1;
    }

    out_buffer[0] = '\0';

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
    // Keep the worst-case blocking time bounded instead of stalling.
    int retries = 3;

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
            Sleep(200);
            retries--;
        }
    }

    if (sock == INVALID_SOCKET) {
        printf("[send_to_server] Error: failed to connect, err=%lu\n", (unsigned long)WSAGetLastError());
        fflush(stdout);
        WSACleanup();
        return -1;
    }

    bb_tls_t tls;
    if (tls_connect_socket(sock, &tls) != 0) {
        printf("[send_to_server] TLS handshake or fingerprint verification failed "
               "(is the server running the TLS build?)\n");
        LOGF("[Daemon] TLS connect to server failed (see client log for details)");
        closesocket(sock);
        WSACleanup();
        return -1;
    }

    printf("[send_to_server] Connected via TLS, sending command...\n");
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
        bb_tls_free(&tls);
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

    int sent = bb_tls_write(&tls, send_buf, (size_t)(sp - send_buf));
    free(send_buf);
    if (sent <= 0) {
        printf("[send_to_server] send() failed\n");
        bb_tls_free(&tls);
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
        int n = bb_tls_read(&tls, &c, 1);
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
                int n = bb_tls_read(&tls, &c, 1);
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
                int n = bb_tls_read(&tls, &c, 1);
                if (n <= 0) break;
                *out++ = c;
                count++;
                remaining--;
            }
            *out = '\0';

            // Consume trailing newline after TLV value
            {
                char nl;
                int n = bb_tls_read(&tls, &nl, 1);
                if (n == 1 && nl == '\r') {
                    bb_tls_read(&tls, &nl, 1); // consume \n after \r
                }
            }
        }
        result = 0;
    } else if (strcmp(status_buf, "ERROR") == 0) {
        char len_line[32] = {0};
        size_t len_pos = 0;
        int complete_length = 0;
        while (len_pos < sizeof(len_line) - 1) {
            char c;
            if (bb_tls_read(&tls, &c, 1) != 1) break;
            if (c == '\n') { complete_length = 1; break; }
            len_line[len_pos++] = c;
        }
        char* end;
        long expected_len = strtol(len_line, &end, 10);
        if (complete_length && end != len_line && *end == '\0' &&
            expected_len > 0 && (size_t)expected_len < buffer_size) {
            size_t got = 0;
            while (got < (size_t)expected_len) {
                int n = bb_tls_read(&tls, out_buffer + got, (size_t)expected_len - got);
                if (n <= 0) break;
                got += (size_t)n;
            }
            out_buffer[got] = '\0';
            char nl = 0;
            if (got == (size_t)expected_len && bb_tls_read(&tls, &nl, 1) == 1 &&
                nl == '\n' && strcmp(out_buffer, "invalid token") == 0) {
                result = SERVER_INVALID_TOKEN;
            }
        }
    }

    bb_tls_free(&tls);
    closesocket(sock);
    WSACleanup();
    return result;
}

// Protect both caches; rejected persisted credentials stay ignored even if a
// config write fails, so they cannot override a replacement held in memory.
static char g_client_token[256] = {0};
static int g_ignore_persisted_token = 0;
static SRWLOCK g_token_lock = SRWLOCK_INIT;
static SRWLOCK g_token_fetch_lock = SRWLOCK_INIT;
static ULONGLONG g_next_token_fetch = 0;

static void get_client_token_locked(char* buf, size_t size) {
    buf[0] = '\0';
    if (g_ignore_persisted_token ||
        !ini_get_string("client", "token", buf, size) || buf[0] == '\0') {
        snprintf(buf, size, "%s", g_client_token);
    }
}

static void get_client_token(char* buf, size_t size) {
    AcquireSRWLockExclusive(&g_token_lock);
    get_client_token_locked(buf, size);
    ReleaseSRWLockExclusive(&g_token_lock);
}

static void set_client_token(const char* token) {
    AcquireSRWLockExclusive(&g_token_lock);
    snprintf(g_client_token, sizeof(g_client_token), "%s", token);
    g_ignore_persisted_token = ini_set_string("client", "token", token) != 0;
    ReleaseSRWLockExclusive(&g_token_lock);
}

static void invalidate_client_token(const char* rejected) {
    AcquireSRWLockExclusive(&g_token_lock);
    char current[256];
    get_client_token_locked(current, sizeof(current));
    // A late rejection of an older request must not discard a newer token.
    if (strcmp(current, rejected) == 0) {
        g_client_token[0] = '\0';
        g_ignore_persisted_token = 1;
        ini_set_string("client", "token", "");
    }
    ReleaseSRWLockExclusive(&g_token_lock);
}

#ifdef BB_TOKEN_TEST
static int token_request(const char*, const char**, size_t, char*, size_t);
#else
#define token_request send_to_server_tlv
#endif

static void certificate_state_locked(char address[64], int* port, char pinned[65]) {
    snprintf(address, 64, "%s", g_server_ip);
    char value[16] = {0};
    ini_get_string_locked("server", "port", value, sizeof(value));
    *port = value[0] ? atoi(value) : 1984;
    if (*port < 1 || *port > 65535) *port = 1984;
    pinned[0] = '\0';
    ini_get_string_locked("server", "fingerprint", pinned, 65);
}

#ifdef BB_CERTIFICATE_TEST
static int certificate_probe(const char*, int, const char*, char[65]);
#else
#define certificate_probe bb_probe_certificate
#endif

int InspectServerCertificate(char address[64], int* port, char pinned[65], char observed[65]) {
    config_lock_init();
    EnterCriticalSection(&g_config_lock);
    certificate_state_locked(address, port, pinned);
    LeaveCriticalSection(&g_config_lock);
    return certificate_probe(address, *port, "", observed);
}

static int certificate_state_matches(const char* address, int port, const char* pin) {
    char current_address[64], current_pin[65];
    int current_port;
    certificate_state_locked(current_address, &current_port, current_pin);
    return strcmp(address, current_address) == 0 && port == current_port &&
           _stricmp(pin, current_pin) == 0;
}

int UpdateServerFingerprint(const char* address, int port, const char* original_pin,
                            const char* verified_fingerprint) {
    char verified[65], observed[65];
    if (!address || !original_pin ||
        bb_normalize_fingerprint(verified_fingerprint, verified) != 0) return -1;
    config_lock_init();
    EnterCriticalSection(&g_config_lock);
    int unchanged = certificate_state_matches(address, port, original_pin);
    LeaveCriticalSection(&g_config_lock);
    if (!unchanged || certificate_probe(address, port, verified, observed) != 0 ||
        _stricmp(verified, observed) != 0) return -1;
    EnterCriticalSection(&g_config_lock);
    int result = -1;
    if (certificate_state_matches(address, port, original_pin)) {
        result = ini_set_string_locked("server", "fingerprint", verified);
        if (result == 0) InterlockedIncrement(&g_certificate_revision);
    }
    LeaveCriticalSection(&g_config_lock);
    return result;
}

// Inspection never calls this helper: only normal connections may establish
// initial trust. Recheck the current pin before accepting concurrent changes.
static int tls_connect_socket(SOCKET sock, bb_tls_t* tls) {
    char pinned[65] = {0}, got[65] = {0};
    ini_get_string("server", "fingerprint", pinned, sizeof(pinned));
    if (bb_tls_init(tls) != 0) return -1;
    if (bb_tls_connect(tls, sock, pinned, got, sizeof(got)) != 0) {
        bb_tls_free(tls);
        return -1;
    }
    config_lock_init();
    EnterCriticalSection(&g_config_lock);
    char current[65] = {0};
    ini_get_string_locked("server", "fingerprint", current, sizeof(current));
    int accepted = current[0] ? _stricmp(current, got) == 0 :
        ini_set_string_locked("server", "fingerprint", got) == 0;
    LeaveCriticalSection(&g_config_lock);
    if (!accepted) { bb_tls_free(tls); return -1; }
    return 0;
}

// Path of the client's persistent ECDSA identity key (beside the executable).
static const char* sig_key_path(void) {
    static char p[MAX_PATH] = {0};
    if (p[0] == '\0') {
        char exe[MAX_PATH];
        get_exe_path(exe, sizeof(exe));
        snprintf(p, sizeof(p), "%s\\client_sigkey.key", exe);
    }
    return p;
}

// Ensures the identity key exists and fills out with its uncompressed P-256
// public point as hex (130 chars). Returns 0 on success.
static int client_identity_pub_hex(char* out, size_t cap) {
    mbedtls_pk_context pk;
    if (client_key_ensure(sig_key_path(), &pk) != 0) {
        LOGF("[Daemon] Failed to create/load client identity key");
        return -1;
    }
    int r = client_key_public_point_hex(&pk, out, cap);
    if (r != 0) LOGF("[Daemon] Failed to export client identity public key");
    mbedtls_pk_free(&pk);
    return r;
}

// Fetches the per-user token automatically when the client has no token yet.
// Flow: GET_TOKEN_CHALLENGE -> sign(username||nonce) -> GET_TOKEN -> save.
// Before approval the server refuses (user not found / key not registered),
// which is fine — REGISTER already carried the public key; we retry each loop.
static int fetch_token_if_needed(const char* username) {
    if (!username || username[0] == '\0') return -1;
    AcquireSRWLockExclusive(&g_token_fetch_lock);
    char token[256];
    get_client_token(token, sizeof(token));
    if (token[0]) {
        ReleaseSRWLockExclusive(&g_token_fetch_lock);
        return 0;
    }
    if (GetTickCount64() < g_next_token_fetch) {
        ReleaseSRWLockExclusive(&g_token_fetch_lock);
        return -1;
    }
    g_next_token_fetch = GetTickCount64() + 5000;
    int result = -1;
    mbedtls_pk_context pk;
    if (client_key_ensure(sig_key_path(), &pk) != 0) {
        LOGF("[Daemon] Token fetch: cannot ensure identity key");
        ReleaseSRWLockExclusive(&g_token_fetch_lock);
        return -1;
    }
    const char* chal_args[] = {username};
    char nonce[256] = {0};
    if (token_request("GET_TOKEN_CHALLENGE", chal_args, 1, nonce, sizeof(nonce)) != 0 || !nonce[0])
        goto done;
    char msg[384], sighex[300];
    int len = snprintf(msg, sizeof(msg), "%s%s", username, nonce);
    if (len < 0 || (size_t)len >= sizeof(msg) ||
        client_key_sign(&pk, (const unsigned char*)msg, (size_t)len, sighex, sizeof(sighex)) != 0)
        goto done;
    const char* tok_args[] = {username, nonce, sighex};
    char response[128] = {0};
    if (token_request("GET_TOKEN", tok_args, 3, response, sizeof(response)) != 0 ||
        !response[0] || strcmp(response, "NOT_SET") == 0)
        goto done;
    set_client_token(response);
    g_next_token_fetch = 0;
    result = 0;
    LOGF("[Daemon] Token received from server automatically");
done:
    mbedtls_pk_free(&pk);
    ReleaseSRWLockExclusive(&g_token_fetch_lock);
    return result;
}

static int send_authenticated_request(const char* command, const char* username,
                                      const char** args, size_t count, size_t token_index,
                                      char* response, size_t capacity) {
    char token[256];
    const char* original_token = args[token_index];
    get_client_token(token, sizeof(token));
    args[token_index] = token;
    int result = token_request(command, args, count, response, capacity);
    args[token_index] = original_token;
    if (result != SERVER_INVALID_TOKEN) return result;
    invalidate_client_token(token);
    if (fetch_token_if_needed(username) != 0) return -1;
    get_client_token(token, sizeof(token));
    args[token_index] = token;
    result = token_request(command, args, count, response, capacity);
    args[token_index] = original_token;
    if (result == SERVER_INVALID_TOKEN) {
        invalidate_client_token(token);
        AcquireSRWLockExclusive(&g_token_fetch_lock);
        g_next_token_fetch = GetTickCount64() + 5000;
        ReleaseSRWLockExclusive(&g_token_fetch_lock);
    }
    return result; // At most one signed fetch and one retry per request.
}

int ServerGetUpdate(char* output, size_t size) {
    char username[128] = {0}, token[256] = {0};
    if (LoadUserName(username, sizeof(username)) != 0 || !username[0]) return -1;
    get_client_token(token, sizeof(token));
    const char* args[] = { username, token };
    int result = send_authenticated_request("GET_CLIENT_UPDATE_DETAILS", username, args, 2, 1, output, size);
    if (result != 0 && strstr(output, "unknown command") != NULL) {
        output[0] = '\0';
        return send_authenticated_request("GET_CLIENT_UPDATE", username, args, 2, 1, output, size);
    }
    return result;
}

int ServerRegister(const char* name) {
    if (!name) return -1;

    char local_ip[64] = {0};
    GetLocalIp(g_server_ip, local_ip, sizeof(local_ip));

    // Client identity public key (ECDSA P-256 point, hex), bound server-side at
    // approval and later used to authorize automatic token delivery.
    char sigpub[160] = {0};
    client_identity_pub_hex(sigpub, sizeof(sigpub));

    const char* args[3] = {name, local_ip, sigpub};
    size_t arg_count = local_ip[0] ? 3 : (sigpub[0] ? 2 : 1);
    char response[256];
    memset(response, 0, sizeof(response));

    DLOGF("ServerRegister: local_ip='%s', pub='%s', sending to server...", local_ip, sigpub);

    int result = send_to_server_tlv("REGISTER", args, arg_count, response, sizeof(response));

    DLOGF("ServerRegister: send_to_server done, result=%d", result);

    if (result == 0) {
        DLOGF("ServerRegister: response='%s'", response);
    }

    return result;
}

int ServerConnect(const char* name) {
    if (!name) return -1;
    char local_ip[64] = {0};
    GetLocalIp(g_server_ip, local_ip, sizeof(local_ip));
    char token[256] = {0};
    get_client_token(token, sizeof(token));
    const char* args[3] = {name, local_ip, token};
    char response[256] = {0};
    LOGF("[Daemon] CONNECT with token len=%zu (ip='%s')", strlen(token), local_ip);
    int result = send_authenticated_request("CONNECT", name, args, 3, 2, response, sizeof(response));
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
    char token[256] = {0};
    get_client_token(token, sizeof(token));
    const char* args[2] = {name, token};
    char response[256];
    int result = send_authenticated_request("DISCONNECT", name, args, 2, 1, response, sizeof(response));
    if (result == 0) SetServerSessionActive(0);
    return result;
}

int ServerRefresh(const char* name) {
    if (!name) return -1;
    char token[256] = {0};
    get_client_token(token, sizeof(token));
    const char* args[2] = {name, token};
    char response[256];
    int result = send_authenticated_request("REFRESH", name, args, 2, 1, response, sizeof(response));
    if (result == 0) SetServerSessionActive(1);
    return result;
}

int ServerChangeName(const char* oldName, const char* newName) {
    if (!oldName || !newName) return -1;
    char token[256] = {0};
    get_client_token(token, sizeof(token));
    const char* args[3] = {oldName, newName, token};
    char response[256];
    return send_authenticated_request("CHANGE_NAME", oldName, args, 3, 2, response, sizeof(response));
}

int PingServer(void) {
    char response[256];
    return send_to_server_tlv("PING", NULL, 0, response, sizeof(response));
}

// --- Event-driven server subscription helpers ---

// Buffered line reader used by server_subscribe_loop
typedef struct {
    bb_tls_t* tls;
    char buf[4096];
    size_t pos;
    size_t len;
} SrvLineReader;

static void srv_line_reader_init(SrvLineReader* r, bb_tls_t* tls) {
    r->tls = tls;
    r->pos = 0;
    r->len = 0;
}

// Read a single LF-terminated line (strips CR). Returns 1 on success,
// 2 on read timeout, 0 on error/disconnect. Over-long lines are truncated
// but fully drained so framing stays intact (no bogus follow-up line).
static int srv_readline(SrvLineReader* r, char* out, size_t out_size) {
    size_t consumed = 0;
    for (;;) {
        if (r->pos >= r->len) {
            r->pos = 0;
            r->len = 0;
            int n = bb_tls_read(r->tls, r->buf, sizeof(r->buf) - 1);
            if (n <= 0) {
                if (n == 0) return 0;         // connection closed
                if (n == BB_TLS_TRY_AGAIN) return 2; // idle timeout, retry
                return 0;                     // real error
            }
            r->len = (size_t)n;
        }
        char c = r->buf[r->pos++];
        if (c == '\n') {
            out[consumed] = '\0';
            return 1;
        }
        if (c == '\r') continue;
        if (consumed + 1 < out_size) {
            out[consumed++] = c;
        }
    }
}

// Read a TLV value (length line + data line) via line reader.
static int srv_read_tlv(SrvLineReader* r, char* out, size_t out_size) {
    char len_str[32];
    if (srv_readline(r, len_str, sizeof(len_str)) != 1) return 0;
    char* end;
    long expected = strtol(len_str, &end, 10);
    if (end == len_str || *end != '\0' || expected <= 0 || (size_t)expected >= out_size) return 0;
    // Read exactly expected bytes
    size_t got = 0;
    while (got < (size_t)expected) {
        if (r->pos >= r->len) {
            int n = bb_tls_read(r->tls, r->buf, sizeof(r->buf) - 1);
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
        int n = bb_tls_read(r->tls, r->buf, sizeof(r->buf) - 1);
        if (n <= 0) return 0;
        r->pos = 0;
        r->len = (size_t)n;
    }
    if (r->buf[r->pos] == '\r') r->pos++;
    if (r->pos >= r->len || r->buf[r->pos] != '\n') return 0;
    r->pos++;
    return 1;
}

// Fetches and applies the whitelist from the server.
static void sync_whitelist_from_server(const char* username) {
    char token[256] = {0};
    get_client_token(token, sizeof(token));
    const char* wl_args[2] = {username, token};

    // Server whitelist can be large; read it into a heap buffer (bounded only by
    // the DAEMON_MAX_MESSAGE_SIZE soft cap).
    char* response = malloc(DAEMON_MAX_MESSAGE_SIZE);
    if (!response) {
        LOGF("[Daemon] Out of memory fetching whitelist");
        return;
    }

    if (send_authenticated_request("GET_WHITELIST", username, wl_args, 2, 1, response, DAEMON_MAX_MESSAGE_SIZE) != 0) {
        LOGF("[Daemon] Failed to fetch whitelist from server");
        free(response);
        return;
    }

    // Server whitelist sync disabled
    if (strcmp(response, "SYNC_DISABLED") == 0) {
        if (!wl_last_equals("SYNC_DISABLED")) {
            if (g_fallback_whitelist_enabled) {
                LOGF("[Daemon] Server whitelist sync is disabled, keeping local whitelist");
            } else {
                LOGF("[Daemon] Server whitelist sync is disabled, fallback off - blocking all traffic");
                char ok_buf[16];
                if (DaemonSetWhitelist("", 0, ok_buf, sizeof(ok_buf)) == 0) {
                    g_whitelist_revision++;
                    g_blocked_all_pushed = 1;
                    NotifyStateChanged();
                }
            }
            wl_last_set("SYNC_DISABLED");
        }
        free(response);
        return;
    }

    if (g_blocked_all_pushed) {
        g_blocked_all_pushed = 0;
        wl_last_clear();
    }
    if (wl_last_equals(response)) {
        free(response);
        return;
    }
    wl_last_set(response);

    char ok_buf[16];
    if (DaemonSetWhitelist(response, strlen(response), ok_buf, sizeof(ok_buf)) != 0) {
        LOGF("[Daemon] Failed to set whitelist on daemon");
    } else {
        LOGF("[Daemon] Whitelist updated");
        g_whitelist_revision++;
        NotifyStateChanged();
    }
    free(response);
}

// Syncs filtration state from the server.
static void sync_filtration_from_server(void) {
    char filt_buf[16] = {0};
    if (send_to_server_tlv("GET_FILTRATION", NULL, 0, filt_buf, sizeof(filt_buf)) != 0) return;
    int server_filt = (filt_buf[0] == '1');
    // With connection-sync enabled the user's explicit choice survives
    // reconnects, so the server value may only pull filtration off.
    if (IsFiltrationAutoDisableEnabled() && server_filt) return;
    if (server_filt != g_filtration_requested) {
        LOGF("[Daemon] Server filtration %s, updating local filtration", server_filt ? "enabled" : "disabled");
        DaemonSetFiltration(server_filt);
    }
}

// Blocks all traffic if the server is unreachable and fallback is disabled.
static void block_all_if_no_fallback(void) {
    if (g_fallback_whitelist_enabled || g_blocked_all_pushed) return;
    char ok_buf[16];
    LOGF("[Daemon] Server unreachable and fallback off - blocking all traffic");
    if (DaemonSetWhitelist("", 0, ok_buf, sizeof(ok_buf)) == 0) {
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
                    LOGF("[Daemon] Server filtration toggled to %d, updating local filtration", val);
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
static int server_subscribe_loop(const char* username, int* consecutive_failures, int allow_auth_retry) {
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

    // Establish TLS over the connected socket (fingerprint pinned).
    LONG certificate_revision = InterlockedCompareExchange(&g_certificate_revision, 0, 0);
    bb_tls_t subscription_tls;
    if (tls_connect_socket(sock, &subscription_tls) != 0) {
        LOGF("[Daemon] SUBSCRIBE TLS handshake failed");
        closesocket(sock);
        WSACleanup();
        return 0;
    }

    // Send SUBSCRIBE with username + client token
    char token[256] = {0};
    get_client_token(token, sizeof(token));
    char sub_req[128 + 512];
    int sub_len = snprintf(sub_req, sizeof(sub_req), "SUBSCRIBE\n%zu\n%s\n%zu\n%s\n\n",
                           strlen(username), username, strlen(token), token);
    if (bb_tls_write(&subscription_tls, sub_req, (size_t)sub_len) <= 0) {
        bb_tls_free(&subscription_tls);
        closesocket(sock);
        WSACleanup();
        return 0;
    }

    // Read OK response
    SrvLineReader reader;
    srv_line_reader_init(&reader, &subscription_tls);
    char status[32] = {0};
    if (!srv_readline(&reader, status, sizeof(status)) || strcmp(status, "OK") != 0) {
        char emsg[256] = {0};
        int rejected = strcmp(status, "ERROR") == 0 &&
            srv_read_tlv(&reader, emsg, sizeof(emsg)) && strcmp(emsg, "invalid token") == 0;
        if (emsg[0] != '\0') {
            LOGF("[Daemon] SUBSCRIBE failed: '%s'", emsg);
        } else {
            LOGF("[Daemon] SUBSCRIBE handshake failed: '%s'", status[0] ? status : "(closed)");
        }
        bb_tls_free(&subscription_tls);
        closesocket(sock);
        WSACleanup();
        if (rejected) {
            invalidate_client_token(token);
            if (allow_auth_retry && fetch_token_if_needed(username) == 0)
                return server_subscribe_loop(username, consecutive_failures, 0);
            if (!allow_auth_retry) {
                AcquireSRWLockExclusive(&g_token_fetch_lock);
                g_next_token_fetch = GetTickCount64() + 5000;
                ReleaseSRWLockExclusive(&g_token_fetch_lock);
            }
        }
        return 0;
    }
    // Consume empty line terminator
    char empty_line[4];
    srv_readline(&reader, empty_line, sizeof(empty_line));

    LOGF("[Daemon] Subscribed to server events for user '%s'", username);

    // On successful subscribe, ensure we are connected and synced
    if (ServerConnect(username) == 0) {
        SetServerSessionActive(1);
    } else {
        apply_filtration_sync();
    }
    sync_whitelist_from_server(username);
    sync_filtration_from_server();
    if (consecutive_failures) *consecutive_failures = 0;

    // Event loop
    DWORD last_heartbeat = GetTickCount();
    DWORD last_local_check = GetTickCount();
    int keep_going = 1;

    while (keep_going) {
        if (certificate_revision != InterlockedCompareExchange(&g_certificate_revision, 0, 0)) break;
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
            if (bb_tls_write(&subscription_tls, "K\n", 2) <= 0) {
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

    bb_tls_free(&subscription_tls);
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

static DWORD WINAPI notification_monitor(LPVOID context) {
    (void)context;
    for (;;) {
        DaemonGetFiltration();
        if (g_ServiceStopEvent != INVALID_HANDLE_VALUE) {
            if (WaitForSingleObject(g_ServiceStopEvent, 1000) != WAIT_TIMEOUT) return 0;
        } else {
            Sleep(1000);
        }
    }
}

static void notification_failed(DWORD error) {
    LOGF("[Notifications] Windows toast delivery failed: 0x%08lX", error);
}

int DaemonRun(const char* server_ip) {
    bb_notifications_set_error_handler(notification_failed);
    HANDLE notification_thread = CreateThread(NULL, 0, notification_monitor, NULL, 0, NULL);
    if (notification_thread) CloseHandle(notification_thread);
    else LOGF("[Notifications] Could not start firewall state monitor");
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
        fetch_token_if_needed(username);
        if (ServerConnect(username) == 0) {
            LOGF("[Daemon] Connected to server successfully");
            SetServerSessionActive(1);
        } else {
            LOGF("[Daemon] Failed to connect to server (may not be registered/approved yet)");
            apply_filtration_sync();
            if (!g_registration_tried) {
                LOGF("[Daemon] Attempting to register...");
                ServerRegister(username);
                g_registration_tried = 1;
            }
        }
    } else {
        LOGF("[Daemon] No saved username found, skipping server connection");
        apply_filtration_sync();
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
        fetch_token_if_needed(username);
        int sub_result = server_subscribe_loop(username, &consecutive_failures, 1);
        if (sub_result == 1) {
            // Service stop requested
            break;
        }

        // Connection lost — clean up state
        LOGF("[Daemon] Lost connection to server %s, retrying...", g_server_ip);
        SetServerSessionActive(0); // syncs filtration off when connection-sync is enabled
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
