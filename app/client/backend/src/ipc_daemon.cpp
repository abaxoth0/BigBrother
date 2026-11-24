/**
 * @file ipc_daemon.cpp
 * @brief Persisted settings, firewall synchronization and authenticated server subscriptions.
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include "../include/ipc_daemon.h"
#include "../include/ipc_client.h"
#include "../include/tls_client.h"
#include "../include/client_key.h"
#include "../include/certificate_pin.h"
#include "log/log.h"
#include "file_security.h"
#include "notify.h"
#include "process_monitors.h"
#include <charconv>
#include "firewall.h"
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <mbedtls/sha256.h>

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

/** @brief Initialize the process-wide config critical section through InitOnceExecuteOnce(). */
static BOOL CALLBACK initialize_config_lock(PINIT_ONCE once, PVOID param, PVOID* context) {
    (void)once; (void)param; (void)context;
    InitializeCriticalSection(&g_config_lock);
    return TRUE;
}

/** @brief Ensure the config critical section exists before any thread attempts to enter it. */
static void config_lock_init(void) {
    InitOnceExecuteOnce(&g_config_once, initialize_config_lock, NULL, NULL);
}

static int ini_get_string_locked(const char* section, const char* key, char* out, size_t out_size);
static int ini_set_string_locked(const char* section, const char* key, const char* value);

static char g_server_ip[64] = {};
static volatile LONG g_server_session_active = 0;
static volatile LONG g_server_connection_paused = 0;
static volatile LONG g_server_connection_revision = 0;
static int g_registration_tried = 0;
static std::atomic<int> g_fallback_whitelist_enabled{1};
// Desired filtration state (set explicitly by the user via the "Вкл/Выкл"
// button, or by the server). The effective state applied to the firewall also
// depends on the server connection when connection-sync is enabled.
static std::atomic<int> g_filtration_requested{1};
// Effective filtration state last pushed to the firewall.
static std::atomic<int> g_filtration_enabled{1};

static int apply_filtration_sync(void);

static std::mutex g_whitelist_mutex;
static std::optional<std::string> g_last_whitelist;
static std::atomic<int> g_blocked_all_pushed{0};
static std::atomic<bool> g_whitelist_sync_pending{true};
static void block_all_if_no_fallback(void);

/** @brief Copy the last downloaded whitelist or sync marker into the mutex-protected cache. */
static void wl_last_set(const char* value) {
    std::lock_guard guard(g_whitelist_mutex);
    g_last_whitelist = value;
}

/** @brief Invalidate the mutex-protected whitelist cache so equal downloads can be reapplied. */
static void wl_last_clear(void) {
    std::lock_guard guard(g_whitelist_mutex);
    g_last_whitelist.reset();
}

/** @brief Compare a non-null whitelist string with the cached value while holding its mutex. */
static int wl_last_equals(const char* value) {
    std::lock_guard guard(g_whitelist_mutex);
    return g_last_whitelist && *g_last_whitelist == value;
}

void ResetFirewallSync(void) {
    wl_last_clear();
    g_blocked_all_pushed = 0;
    g_whitelist_sync_pending = true;
    if (!IsServerSessionActive()) block_all_if_no_fallback();
    RefreshFiltrationEffective();
}

int IsWindowsNotificationsEnabled(void) {
    char value[8] = {};
    return !ini_get_string("notifications", "enabled", value, sizeof(value)) || strcmp(value, "0") != 0;
}

int SetWindowsNotificationsEnabled(int enabled) {
    return ini_set_string("notifications", "enabled", enabled ? "1" : "0");
}

/**
 * @brief Submit a Windows notification only when the persisted preference permits it.
 * @return 0 when disabled or submission succeeds; otherwise the notification helper error.
 */
static int notify_windows(const char* title, const char* message) {
    return IsWindowsNotificationsEnabled() ? bb_notify(title, message) : 0;
}

void SetServerSessionActive(int active) {
    active = active ? 1 : 0;
    if (active && InterlockedCompareExchange(&g_server_connection_paused, 0, 0)) active = 0;
    if (InterlockedExchange(&g_server_session_active, active) != active) {
        if (notify_windows("Соединение с сервером", active ? "Подключено к серверу" : "Соединение с сервером потеряно") != 0)
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

void ResumeServerConnection(void) {
    InterlockedIncrement(&g_server_connection_revision);
    InterlockedExchange(&g_server_connection_paused, 0);
}

/** @brief Atomically check whether manual or server-requested disconnection inhibits reconnect. */
static int is_server_connection_paused(void) {
    return InterlockedCompareExchange(&g_server_connection_paused, 0, 0) != 0;
}

/** @brief Pause reconnect, invalidate the current subscription and mark the session inactive. */
static void pause_server_connection(void) {
    InterlockedExchange(&g_server_connection_paused, 1);
    InterlockedIncrement(&g_server_connection_revision);
    SetServerSessionActive(0);
}

void SetFallbackWhitelistEnabled(int enabled) {
    g_fallback_whitelist_enabled = enabled ? 1 : 0;
}

int IsFallbackWhitelistEnabled(void) {
    return g_fallback_whitelist_enabled;
}

uint32_t g_whitelist_revision = 1;

/**
 * @brief Write the executable directory, retaining its trailing backslash.
 * @pre buffer is large enough for the complete module path and its terminating NUL.
 */
static void get_exe_path(char* buffer, size_t size) {
    GetModuleFileName(NULL, buffer, (DWORD)size);
    char* p = buffer + strlen(buffer);
    while (p > buffer && *(p - 1) != '\\') p--;
    *p = '\0';
}

/**
 * @brief Append the relative config path to the executable directory.
 * @pre buffer has room for the full executable directory and config path, including NUL.
 */
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

    const char* roots[] = {"WINDIR", "PROGRAMFILES", "PROGRAMDATA", "USERPROFILE", NULL};
    for (const char** name = roots; *name; name++) {
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

/**
 * @brief Trim leading spaces/tabs and trailing spaces/tabs/CR/LF in a mutable string.
 * @return Borrowed pointer inside s; the trailing whitespace is replaced with NUL.
 */
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

/**
 * @brief Read a case-insensitive INI key and recover misplaced legacy credentials when possible.
 * @pre The initialized g_config_lock is held; out_size is positive and out is writable.
 * @return 1 if found (possibly truncated), 0 if absent or unavailable. Ambiguous or invalid
 * legacy credentials produce a nonempty rejection marker rather than an absent value.
 * @note May create the config file or rewrite a recovered credential into its proper section.
 */
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

    char recovered[65] = {};
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

/**
 * @brief Rewrite one INI value while preserving unrelated sections and keys.
 * @pre The initialized g_config_lock is held; section, key and value are non-null strings.
 * @return 0 after writing the file, -1 on allocation or open failure.
 */
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
                char** new_lines = static_cast<char**>(realloc(lines, (line_count + 1) * sizeof(char*)));
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
    char buf[64] = {};
    if (ini_get_string("server", "address", buf, sizeof(buf)) && buf[0]) {
        config_lock_init();
        EnterCriticalSection(&g_config_lock);
        snprintf(g_server_ip, sizeof(g_server_ip), "%s", buf);
        LeaveCriticalSection(&g_config_lock);
        DLOGF("load_server_ip: loaded IP = '%s'", buf);
    } else {
        DLOGF("load_server_ip: no config or empty");
    }
}

const char* GetServerIp(void) {
    thread_local char snapshot[64];
    config_lock_init();
    EnterCriticalSection(&g_config_lock);
    snprintf(snapshot, sizeof(snapshot), "%s", g_server_ip);
    LeaveCriticalSection(&g_config_lock);
    return snapshot;
}

int HasServerIp(void) {
    return GetServerIp()[0] != '\0';
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

/**
 * @brief Forward a legacy daemon request through FirewallModule::request().
 * Despite the helper name, the firewall protocol concatenates string arguments after
 * the command line; it does not use the frontend's length-prefixed framing.
 * @return 0 with a NUL-terminated reply, -1 on invalid input, capacity or transport failure.
 */
static int send_command_tlv(const char* command, const char** args, size_t count,
                           char* output, size_t capacity) {
    return client::FirewallModule::request(command, args, count, output, capacity);
}

int DaemonGetStatus(char* out_buffer, size_t buffer_size) {
    return send_command_tlv("GET_STATUS", NULL, 0, out_buffer, buffer_size);
}

int DaemonGetWhitelist(char* out_buffer, size_t buffer_size) {
    return send_command_tlv("GET_WHITELIST", NULL, 0, out_buffer, buffer_size);
}

int DaemonSetWhitelist(const char* data, size_t, char* out_buffer, size_t buffer_size) {
    const char* args[1] = {data};
    int result = send_command_tlv("SET_WHITELIST", args, 1, out_buffer, buffer_size);
    if (result == 0 && strcmp(out_buffer, "OK") == 0 &&
        notify_windows("Белый список", "Белый список обновлён") != 0)
        LOGF("[Notifications] Could not launch whitelist notification");
    if (result == 0 && strcmp(out_buffer, "OK") != 0) return -1;
    return result;
}

int DaemonReloadWhitelist(char* out_buffer, size_t buffer_size) {
    int result = send_command_tlv("RELOAD", NULL, 0, out_buffer, buffer_size);
    if (result == 0 && strcmp(out_buffer, "OK") == 0 &&
        notify_windows("Белый список", "Белый список перезагружен") != 0)
        LOGF("[Notifications] Could not launch whitelist notification");
    if (result == 0 && strcmp(out_buffer, "OK") != 0) return -1;
    return result;
}

int PingDaemon(void) {
    char buffer[256];
    return send_command_tlv("PING", NULL, 0, buffer, sizeof(buffer));
}

/**
 * @brief Request the firewall log path into a buffer whose capacity includes NUL.
 * @return 0 when the reply is received, -1 on transport or output-capacity failure.
 */
int DaemonGetLogPath(char* out_buffer, size_t buffer_size) {
    return send_command_tlv("GET_LOG_PATH", NULL, 0, out_buffer, buffer_size);
}

static volatile LONG g_notification_filtration = -1;

/** @brief Notify once per effective filtration transition, suppressing the initial observation. */
static void notify_filtration(int enabled) {
    LONG previous = InterlockedExchange(&g_notification_filtration, enabled);
    if (previous != -1 && previous != enabled &&
        notify_windows("Фильтрация", enabled ? "Фильтрация включена" : "Фильтрация выключена") != 0)
        LOGF("[Notifications] Could not launch filtration notification");
}

int DaemonGetFiltration(void) {
    char buf[16] = {};
    if (send_command_tlv("GET_FILTRATION", NULL, 0, buf, sizeof(buf)) == 0) {
        g_filtration_enabled = (buf[0] == '1');
    } else {
        // Firewall is not running — nothing is being filtered, report as off.
        g_filtration_enabled = 0;
    }
    notify_filtration(g_filtration_enabled);
    return g_filtration_enabled;
}

/**
 * @brief Push an already connection-gated state and update the cache/subscribers on acknowledgement.
 * @return 0 if the firewall replies OK, -1 on transport failure or any other reply.
 */
static int daemon_set_filtration_effective(int effective) {
    const char* args[1] = {effective ? "1" : "0"};
    char buf[16] = {};
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
    char buf[8] = {};
    if (ini_get_string("filtration", "block_ipv6", buf, sizeof(buf)) && buf[0])
        return strcmp(buf, "0") != 0;
    return 1;
}

static SRWLOCK g_ipv6_sync_lock = SRWLOCK_INIT;

/**
 * @brief Reconcile the firewall IPv6 setting with the persisted preference.
 * @pre g_ipv6_sync_lock is held exclusively.
 * @return 0 if already equal or successfully updated, -1 on query/update failure.
 */
static int sync_ipv6_block_locked(void) {
    int desired = IsIpv6BlockEnabled();
    char buf[32] = {};
    if (send_command_tlv("GET_IPV6_BLOCK", NULL, 0, buf, sizeof(buf)) != 0)
        return -1;
    if (strcmp(buf, desired ? "1" : "0") == 0) return 0;
    const char* args[] = {desired ? "1" : "0"};
    if (send_command_tlv("SET_IPV6_BLOCK", args, 1, buf, sizeof(buf)) != 0)
        return -1;
    return strcmp(buf, "OK") == 0 ? 0 : -1;
}

/**
 * @brief Serialize IPv6 preference reconciliation against concurrent setting changes.
 * @return 0 if synchronized, -1 if the firewall cannot be queried or updated.
 */
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
    NotifyStateChanged();
    return 0;
}

int IsFiltrationAutoDisableEnabled(void) {
    char buf[8] = {};
    if (ini_get_string("filtration", "auto_disable", buf, sizeof(buf)) && buf[0])
        return buf[0] == '1';
    return 1; // enabled by default
}

void SetFiltrationAutoDisableEnabled(int enabled) {
    ini_set_string("filtration", "auto_disable", enabled ? "1" : "0");
    apply_filtration_sync();
}

/**
 * @brief Gate the requested filtration state on the connection preference and push changes.
 * Preserves the explicit user/server request even when the connection temporarily disables it.
 * @return 0 if unchanged or applied, -1 if the firewall update fails.
 */
static int apply_filtration_sync(void) {
    int effective = (IsFiltrationAutoDisableEnabled() && !IsServerSessionActive())
                        ? 0 : g_filtration_requested.load();
    if (effective != g_filtration_enabled) {
        LOGF("[Daemon] Filtration sync: requested=%d connected=%d -> effective=%d",
             g_filtration_requested.load(), IsServerSessionActive(), effective);
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
                        ? 0 : g_filtration_requested.load();
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

/** @brief Join complete TLV fields with newlines. Reject partial replies and fields that do not fit. */
template<typename Read>
static bool read_server_fields(Read read, char* output, size_t capacity) {
    size_t used = 0;
    bool first = true;
    while (capacity > 0) {
        char length[24];
        size_t digits = 0;
        char c = 0;
        while (read(&c, 1) == 1 && c != '\n') {
            if (digits == sizeof(length)) return false;
            length[digits++] = c;
        }
        if (c != '\n') return false;
        if (digits == 0) {
            output[used] = '\0';
            return true;
        }
        size_t bytes = 0;
        auto parsed = std::from_chars(length, length + digits, bytes);
        if (parsed.ec != std::errc{} || parsed.ptr != length + digits) return false;
        if (!first) {
            if (used >= capacity - 1) return false;
            output[used++] = '\n';
        }
        if (bytes > capacity - 1 - used) return false;
        size_t end = used + bytes;
        while (used < end) {
            int n = read(output + used, end - used);
            if (n <= 0 || memchr(output + used, 0, static_cast<size_t>(n))) return false;
            used += static_cast<size_t>(n);
        }
        if (read(&c, 1) != 1 || c != '\n') return false;
        first = false;
    }
    return false;
}

/**
 * @brief Open a pinned TLS connection, exchange one framed command and release the connection.
 * @param command Non-null command name without framing.
 * @param args Borrowed NUL-terminated argument strings; null entries are skipped.
 * @param arg_count Number of entries in args.
 * @param out_buffer Receives reply fields joined by newlines or a server error message.
 * @param buffer_size Output capacity including NUL; must be positive.
 * @return 0 for an OK status, SERVER_INVALID_TOKEN for a complete invalid-token reply,
 * or -1 for other server/connection failures.
 * A truncated or oversized OK reply returns an error.
 */
static int send_to_server_tlv(const char* command, const char** args, size_t arg_count,
                              char* out_buffer, size_t buffer_size) {
    const std::string server_address = GetServerIp();
    if (!server_address.c_str()[0] || !command || !out_buffer || buffer_size == 0) {
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
    const char* target_ip = server_address.c_str();
    if (strcmp(server_address.c_str(), ".") == 0 || strcmp(server_address.c_str(), "127.0.0.1") == 0) {
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

    char* send_buf = static_cast<char*>(malloc(total_size));
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
    char status_buf[32] = {};
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
        result = read_server_fields([&tls](char* buffer, size_t count) { return bb_tls_read(&tls, buffer, count); },
                                    out_buffer, buffer_size) ? 0 : -1;
        if (result != 0) out_buffer[0] = '\0';
    } else if (strcmp(status_buf, "ERROR") == 0) {
        char len_line[32] = {};
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
static char g_client_token[256] = {};
static int g_ignore_persisted_token = 0;
static SRWLOCK g_token_lock = SRWLOCK_INIT;
static SRWLOCK g_token_fetch_lock = SRWLOCK_INIT;
static ULONGLONG g_next_token_fetch = 0;

/**
 * @brief Read the persisted token unless rejected, falling back to the in-memory token.
 * @pre g_token_lock is held exclusively; buf has positive size including NUL.
 */
static void get_client_token_locked(char* buf, size_t size) {
    buf[0] = '\0';
    if (g_ignore_persisted_token ||
        !ini_get_string("client", "token", buf, size) || buf[0] == '\0') {
        snprintf(buf, size, "%s", g_client_token);
    }
}

/**
 * @brief Copy a NUL-terminated token snapshot while serializing access to both token caches.
 * @pre buf is writable and size is positive; a small buffer receives a truncated token.
 */
static void get_client_token(char* buf, size_t size) {
    AcquireSRWLockExclusive(&g_token_lock);
    get_client_token_locked(buf, size);
    ReleaseSRWLockExclusive(&g_token_lock);
}

/**
 * @brief Cache and attempt to persist a replacement token under g_token_lock.
 * If persistence fails, keep the replacement in memory and ignore stale disk credentials.
 */
static void set_client_token(const char* token) {
    AcquireSRWLockExclusive(&g_token_lock);
    snprintf(g_client_token, sizeof(g_client_token), "%s", token);
    g_ignore_persisted_token = ini_set_string("client", "token", token) != 0;
    ReleaseSRWLockExclusive(&g_token_lock);
}

/**
 * @brief Discard a rejected token only if it still matches the current token.
 * Serializes both caches so a late response cannot discard a newer credential;
 * failed disk clearing still prevents reuse of the rejected persisted value.
 */
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

/**
 * @brief Snapshot the configured address, validated port and stored fingerprint.
 * @pre g_config_lock is held; address and pinned hold 64 and 65 bytes respectively.
 */
static void certificate_state_locked(char address[64], int* port, char pinned[65]) {
    snprintf(address, 64, "%s", g_server_ip);
    char value[16] = {};
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

/**
 * @brief Compare address/port exactly and the saved fingerprint case-insensitively.
 * @pre g_config_lock is held; address and pin are non-null strings.
 */
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

/**
 * @brief Initialize TLS on a borrowed connected socket and enforce or establish the stored pin.
 * Rechecks configuration after the handshake before accepting concurrent pin changes.
 * Only normal connections use this trust-on-first-use path; inspection does not.
 * @return 0 with live TLS state the caller must free, -1 with TLS state cleaned up.
 * The caller retains ownership of sock on either outcome.
 */
static int tls_connect_socket(SOCKET sock, bb_tls_t* tls) {
    char pinned[65] = {0}, got[65] = {};
    ini_get_string("server", "fingerprint", pinned, sizeof(pinned));
    if (bb_tls_init(tls) != 0) return -1;
    if (bb_tls_connect(tls, sock, pinned, got, sizeof(got)) != 0) {
        bb_tls_free(tls);
        return -1;
    }
    config_lock_init();
    EnterCriticalSection(&g_config_lock);
    char current[65] = {};
    ini_get_string_locked("server", "fingerprint", current, sizeof(current));
    int accepted = current[0] ? _stricmp(current, got) == 0 :
        ini_set_string_locked("server", "fingerprint", got) == 0;
    LeaveCriticalSection(&g_config_lock);
    if (!accepted) { bb_tls_free(tls); return -1; }
    return 0;
}

/**
 * @brief Return a borrowed, lazily initialized path to the persistent identity beside the executable.
 * @pre Callers serialize the first access to this static path buffer.
 */
static const char* sig_key_path(void) {
    static char p[MAX_PATH] = {};
    if (p[0] == '\0') {
        char exe[MAX_PATH];
        get_exe_path(exe, sizeof(exe));
        snprintf(p, sizeof(p), "%s\\client_sigkey.key", exe);
    }
    return p;
}

/**
 * @brief Load/create the identity and export its uncompressed P-256 public point as lowercase hex.
 * @pre Identity creation and shared RNG initialization are serialized by callers.
 * @return 0 on success, -1 on key/export failure or capacity below 131 bytes including NUL.
 */
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

/**
 * @brief Obtain a missing token by signing the server challenge and caching the response.
 * Serializes fetches and backs off for five seconds after a failed attempt.
 * @return 0 if a token already exists or was fetched, -1 for missing username, backoff or failure.
 */
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
    char nonce[256] = {};
    char msg[384], sighex[300];
    int len = 0;
    const char* tok_args[] = {username, nonce, sighex};
    char response[128] = {};
    if (token_request("GET_TOKEN_CHALLENGE", chal_args, 1, nonce, sizeof(nonce)) != 0 || !nonce[0])
        goto done;

    len = snprintf(msg, sizeof(msg), "%s%s", username, nonce);
    if (len < 0 || (size_t)len >= sizeof(msg) ||
        client_key_sign(&pk, (const unsigned char*)msg, (size_t)len, sighex, sizeof(sighex)) != 0)
        goto done;

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

/**
 * @brief Inject the current token and retry once after an explicit invalid-token response.
 * @param command Remote command name.
 * @param username Identity used for a signed token refresh.
 * @param args Mutable array of borrowed arguments; the token slot is restored before return.
 * @param count Number of entries in args.
 * @param token_index Existing token slot; must be less than count.
 * @param response Reply buffer following send_to_server_tlv() semantics.
 * @param capacity Reply capacity including NUL.
 * @return Transport result, or -1 if token refresh fails; a second rejection starts fetch backoff.
 */
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
    char username[128] = {0}, token[256] = {};
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

    char local_ip[64] = {};
    GetLocalIp(GetServerIp(), local_ip, sizeof(local_ip));

    // Client identity public key (ECDSA P-256 point, hex), bound server-side at
    // approval and later used to authorize automatic token delivery.
    char sigpub[160] = {};
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
    if (!name || is_server_connection_paused()) return -1;
    char local_ip[64] = {};
    GetLocalIp(GetServerIp(), local_ip, sizeof(local_ip));
    char token[256] = {};
    get_client_token(token, sizeof(token));
    const char* args[3] = {name, local_ip, token};
    char response[256] = {};
    LOGF("[Daemon] CONNECT with token len=%zu (ip='%s')", strlen(token), local_ip);
    int result = send_authenticated_request("CONNECT", name, args, 3, 2, response, sizeof(response));
    if (result == 0 || strstr(response, "already connected") != NULL) {
        SetServerSessionActive(1);
        return 0;
    }
    if (response[0]) {
        LOGF("[ServerConnect] Server error: '%s'", response);
    } else {
        LOGF("[ServerConnect] TCP connection to %s:%d failed", GetServerIp(), get_server_port());
    }
    return result;
}

int ServerDisconnect(const char* name) {
    if (!name) return -1;
    char token[256] = {};
    get_client_token(token, sizeof(token));
    const char* args[2] = {name, token};
    char response[256];
    int result = send_authenticated_request("DISCONNECT", name, args, 2, 1, response, sizeof(response));
    if (result == 0) pause_server_connection();
    return result;
}

int ServerRefresh(const char* name) {
    if (!name || is_server_connection_paused()) return -1;
    char token[256] = {};
    get_client_token(token, sizeof(token));
    const char* args[2] = {name, token};
    char response[256];
    int result = send_authenticated_request("REFRESH", name, args, 2, 1, response, sizeof(response));
    if (result == 0) SetServerSessionActive(1);
    return result;
}

int ServerChangeName(const char* oldName, const char* newName) {
    if (!oldName || !newName) return -1;
    char token[256] = {};
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

/** @brief Subscription reader borrowing TLS state; unread bytes occupy buf[pos..len). */
typedef struct {
    bb_tls_t* tls;
    char buf[4096];
    size_t pos;
    size_t len;
} SrvLineReader;

/** @brief Reset a buffered subscription reader borrowing the caller-owned, connected TLS context. */
static void srv_line_reader_init(SrvLineReader* r, bb_tls_t* tls) {
    r->tls = tls;
    r->pos = 0;
    r->len = 0;
}

/**
 * @brief Read an LF-terminated subscription line, dropping CR bytes and adding NUL.
 * @pre out_size is positive; out has that many writable bytes.
 * @return 1 for a line, 2 for a transient TLS read, 0 for closure/error.
 * Overlong lines are truncated but drained. A non-success result may leave partial
 * output; a transient read does not preserve the partial line for the next call.
 */
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

/**
 * @brief Read one positive-length subscription field and its line separator.
 * @pre out has out_size writable bytes; capacity must exceed the payload length for NUL.
 * @return 1 for a complete field, 0 on invalid length/separator, capacity failure or TLS failure.
 * Partial output and reader state must not be reused as a complete field after failure.
 */
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

/** @brief Fetch a complete authenticated policy before replacing any running monitors. */
static bool sync_process_blacklist(const char* username, client::ProcessMonitors& monitors) {
    char response[8193];
    const char* args[] = {username, nullptr};
    return send_authenticated_request("GET_PROCESS_BLACKLIST", username, args, 2, 1,
                                      response, sizeof(response)) == 0 && monitors.replace(response);
}

/**
 * @brief Download authenticated rules and apply changed content to the local firewall.
 * Honors SYNC_DISABLED and the fallback preference; leaves synchronization pending
 * after download/apply failure so the subscription loop can retry.
 */
static void sync_whitelist_from_server(const char* username) {
    g_whitelist_sync_pending = 1;
    char token[256] = {};
    get_client_token(token, sizeof(token));
    const char* wl_args[2] = {username, token};

    // Server whitelist can be large; read it into a heap buffer (bounded only by
    // the DAEMON_MAX_MESSAGE_SIZE soft cap).
    char* response = static_cast<char*>(malloc(DAEMON_MAX_MESSAGE_SIZE));
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
        g_whitelist_sync_pending = 0;
        free(response);
        return;
    }

    if (g_blocked_all_pushed) {
        g_blocked_all_pushed = 0;
        wl_last_clear();
    }
    if (wl_last_equals(response)) {
        g_whitelist_sync_pending = 0;
        free(response);
        return;
    }
    char ok_buf[16];
    if (DaemonSetWhitelist(response, strlen(response), ok_buf, sizeof(ok_buf)) != 0) {
        LOGF("[Daemon] Failed to set whitelist on daemon");
    } else {
        wl_last_set(response);
        g_whitelist_sync_pending = 0;
        LOGF("[Daemon] Whitelist updated");
        g_whitelist_revision++;
        NotifyStateChanged();
    }
    free(response);
}

/** @brief Compare pointers to normalized rule strings lexicographically for qsort(). */
static int compare_rules(const void* a, const void* b) {
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}

/**
 * @brief Hash normalized, sorted, unique rules using SHA-256.
 * @param rules Mutable NUL-terminated rules; tokenized, trimmed and ASCII-lowercased in place.
 * @param revision Output for 64 lowercase hex digits plus NUL; valid only on success.
 * @return 0 on success, -1 on allocation or hashing failure.
 * Blank/comment lines are ignored; each unique rule contributes one trailing newline.
 */
static int whitelist_revision(char* rules, char revision[65]) {
    size_t capacity = 1;
    for (char* p = rules; *p; p++) if (*p == '\n') capacity++;

    char** entries = static_cast<char**>(malloc(capacity * sizeof(*entries)));
    if (!entries) return -1;

    size_t count = 0;
    char* context;
    for (char* line = strtok_r(rules, "\n", &context); line; line = strtok_r(NULL, "\n", &context)) {
        while (isspace((unsigned char)*line)) line++;
        size_t len = strlen(line);
        while (len && isspace((unsigned char)line[len - 1])) line[--len] = '\0';
        if (!len || line[0] == '#' || line[0] == ';') continue;

        for (char* p = line; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 'a' - 'A';
        entries[count++] = line;
    }

    qsort(entries, count, sizeof(*entries), compare_rules);

    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    int result = mbedtls_sha256_starts(&hash, 0);
    for (size_t i = 0; !result && i < count; i++) {
        if (i && strcmp(entries[i], entries[i - 1]) == 0) continue;
        result = mbedtls_sha256_update(&hash, (unsigned char*)entries[i], strlen(entries[i]));
        if (!result) result = mbedtls_sha256_update(&hash, (const unsigned char*)"\n", 1);
    }

    unsigned char digest[32];
    if (!result) result = mbedtls_sha256_finish(&hash, digest);
    mbedtls_sha256_free(&hash);
    free(entries);
    if (result) return -1;

    for (size_t i = 0; i < sizeof(digest); i++) snprintf(revision + i * 2, 3, "%02x", digest[i]);
    return 0;
}

/**
 * @brief Read the firewall rules and compute their normalized revision into 65 writable bytes.
 * @return 0 on success, -1 with an empty revision if rules are unavailable or invalid.
 */
static int firewall_whitelist_revision(char revision[65]) {
    revision[0] = '\0';
    int result = -1;
    char* whitelist = static_cast<char*>(malloc(DAEMON_MAX_MESSAGE_SIZE));
    if (whitelist) {
        if (DaemonGetWhitelist(whitelist, DAEMON_MAX_MESSAGE_SIZE) == 0) {
            if (strcmp(whitelist, "WHITELIST") == 0)
                result = whitelist_revision(whitelist + strlen(whitelist), revision);
            else if (strncmp(whitelist, "WHITELIST\n", 10) == 0)
                result = whitelist_revision(whitelist + 10, revision);
        }
        free(whitelist);
    }
    return result;
}

/**
 * @brief Refresh local policy and format its newline-terminated JSON heartbeat.
 * Includes an empty revision when firewall rules are unavailable.
 * @return snprintf() length excluding NUL; callers must check it against capacity.
 */
static int build_policy_heartbeat(char* message, size_t capacity) {
    int filtration = RefreshFiltrationEffective();
    char revision[65];
    firewall_whitelist_revision(revision);
    return snprintf(message, capacity,
        "POLICY {\"whitelistRevision\":\"%s\",\"filtrationEnabled\":%s,\"blockIpv6\":%s,\"canSetIpv6Block\":true}\n",
        revision, filtration ? "true" : "false", IsIpv6BlockEnabled() ? "true" : "false");
}

/**
 * @brief Send the current policy heartbeat over a borrowed connected TLS context.
 * @return Bytes written, or -1 on TLS write failure.
 */
static int send_policy_heartbeat(bb_tls_t* tls) {
    char message[256];
    int length = build_policy_heartbeat(message, sizeof(message));
    return bb_tls_write(tls, message, (size_t)length);
}

/**
 * @brief Fetch remote filtration and apply it subject to connection-sync policy.
 * When connection-sync is enabled, a remote enabled state cannot override the local request.
 */
static void sync_filtration_from_server(void) {
    char filt_buf[16] = {};
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

/**
 * @brief Push an empty whitelist once while fallback is disabled and no prior push is recorded.
 * The caller decides when the server is unavailable; failed firewall updates may be retried.
 */
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

/**
 * @brief Find the saved server name via UDP discovery and persist its new address/port.
 * @return 1 on a matching discovery reply, 0 when no match or discovery is unavailable.
 * Persistence errors are not propagated through this return value.
 */
static int try_discover_server(void) {
    char expected_name[128] = {};
    ini_get_string("server", "name", expected_name, sizeof(expected_name));
    if (expected_name[0] == '\0') return 0;

    char local_ip[64] = {0}, bcast_list[4096] = {};
    GetAllBroadcastAddresses(local_ip, sizeof(local_ip), bcast_list, sizeof(bcast_list));
    if (bcast_list[0] == '\0') return 0;

    char disco_resp[8192] = {};
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
            char default_port[] = "1984";
            char* server_port = default_port;
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

/**
 * @brief Read up to eight key=value event lines and apply the matching server policy change.
 * @param event_type Event name already decoded from the preceding TLV field.
 * @param reader Borrowed subscription reader positioned at the event data.
 * @param username Local identity used to filter targeted events.
 * @return 0 to continue, 1 to close a paused subscription, 2 to send a policy heartbeat or 3 to refresh the process blacklist.
 * Unknown events and incomplete data reads also return 0.
 */
static int handle_server_event(const char* event_type, SrvLineReader* reader, const char* username) {
    // Read data key=value lines until empty line
    char data[8][256];
    int data_count = 0;
    while (data_count < 8) {
        char line[256];
        if (srv_readline(reader, line, sizeof(line)) != 1) return 0;
        if (line[0] == '\0') break; // empty line terminates event
        snprintf(data[data_count], sizeof(data[data_count]), "%s", line);
        data[data_count][sizeof(data[data_count]) - 1] = '\0';
        data_count++;
    }

    if (strcmp(event_type, "CLIENT_DISCONNECTED") == 0) {
        for (int i = 0; i < data_count; i++) {
            if (strncmp(data[i], "name=", 5) == 0 && strcmp(data[i] + 5, username) == 0) {
                LOGF("[Daemon] Disconnected by server; automatic reconnect paused");
                pause_server_connection();
                return 1;
            }
        }
        return 0;
    }

    if (strcmp(event_type, "CLIENT_IPV6_BLOCK_CHANGED") == 0) {
        const char* target = NULL;
        int enabled = -1;

        for (int i = 0; i < data_count; i++) {
            if (strncmp(data[i], "name=", 5) == 0) target = data[i] + 5;
            else if (strcmp(data[i], "enabled=0") == 0) enabled = 0;
            else if (strcmp(data[i], "enabled=1") == 0) enabled = 1;
        }
        if (!target || strcmp(target, username) != 0 || enabled < 0) return 0;

        if (SetIpv6BlockEnabled(enabled) != 0)
            LOGF("[Daemon] Failed to save server-requested IPv6 blocking preference");
        else
            LOGF("[Daemon] Server set IPv6 blocking to %d", enabled);
        return 2;
    }

    if (strcmp(event_type, "WHITELIST_SYNC_REQUESTED") == 0) {
        const char* expected = NULL;
        for (int i = 0; i < data_count; i++) {
            if (strncmp(data[i], "revision=", 9) == 0) expected = data[i] + 9;
        }
        if (!expected || strlen(expected) != 64 || strspn(expected, "0123456789abcdef") != 64) return 0;

        char revision[65];
        if (firewall_whitelist_revision(revision) != 0 || strcmp(revision, expected) != 0) {
            LOGF("[Daemon] Whitelist synchronization requested; refreshing mismatched or unavailable rules");
            wl_last_clear();
            sync_whitelist_from_server(username);
        }
        return 2;
    }

    if (strcmp(event_type, "PROCESS_BLACKLIST_CHANGED") == 0) return 3;

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

/**
 * @brief Maintain a pinned, authenticated subscription until it must close or the service stops.
 * Applies pushed policy, sends heartbeats and retries pending whitelist synchronization.
 * @param username Non-null local identity for authentication and event filtering.
 * @param consecutive_failures Optional counter incremented on TCP failure and reset on subscription.
 * @param allow_auth_retry Whether one signed token refresh and subscription retry is permitted.
 * @return 1 when service stop is observed in the event loop; 0 on connection/setup failure,
 * manual pause, configuration revision change or any other subscription termination.
 */
static int server_subscribe_loop(const char* username, int* consecutive_failures, int allow_auth_retry) {
    const std::string server_address = GetServerIp();
    if (is_server_connection_paused()) return 0;
    LONG connection_revision = InterlockedCompareExchange(&g_server_connection_revision, 0, 0);
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;

    const char* target_ip = server_address.c_str();
    if (strcmp(server_address.c_str(), ".") == 0 || strcmp(server_address.c_str(), "127.0.0.1") == 0) {
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
    char token[256] = {};
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
    char status[32] = {};
    if (!srv_readline(&reader, status, sizeof(status)) || strcmp(status, "OK") != 0) {
        char emsg[256] = {};
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

    SetServerSessionActive(1);
    client::ProcessMonitors monitors;
    bool processSynced = sync_process_blacklist(username, monitors);
    DWORD lastProcessFetch = GetTickCount();
    monitors.refresh();
    sync_whitelist_from_server(username);
    sync_filtration_from_server();
    if (consecutive_failures) *consecutive_failures = 0;

    // Event loop
    DWORD last_heartbeat = GetTickCount();
    DWORD last_local_check = GetTickCount();
    int keep_going = send_policy_heartbeat(&subscription_tls) > 0;
    int stop_requested = 0;

    while (keep_going) {
        if (is_server_connection_paused() || connection_revision != InterlockedCompareExchange(&g_server_connection_revision, 0, 0)) break;
        if (certificate_revision != InterlockedCompareExchange(&g_certificate_revision, 0, 0)) break;
        // Check for service stop
        if (g_ServiceStopEvent != INVALID_HANDLE_VALUE &&
            WaitForSingleObject(g_ServiceStopEvent, 0) == WAIT_OBJECT_0) {
            keep_going = 0;
            stop_requested = 1;
            break;
        }

        // Heartbeat every SRV_HEARTBEAT_INTERVAL_MS
        DWORD now = GetTickCount();
        if (now - last_heartbeat >= SRV_HEARTBEAT_INTERVAL_MS) {
            last_heartbeat = now;
            if (send_policy_heartbeat(&subscription_tls) <= 0) {
                LOGF("[Daemon] Heartbeat send failed, connection lost");
                break;
            }
        }

        // Check local daemon health
        if (now - last_local_check >= SRV_LOCAL_DAEMON_CHECK_MS) {
            last_local_check = now;
            if (!processSynced || now - lastProcessFetch >= 60000) {
                processSynced = sync_process_blacklist(username, monitors);
                lastProcessFetch = now;
            }
            monitors.refresh();
            if (g_whitelist_sync_pending) sync_whitelist_from_server(username);
        }

        // Try to read a line (non-blocking with SO_RCVTIMEO)
        char line[512];
        int rd = srv_readline(&reader, line, sizeof(line));
        if (connection_revision != InterlockedCompareExchange(&g_server_connection_revision, 0, 0)) break;
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

            int result = handle_server_event(event_type, &reader, username);
            if (result == 1) {
                break;
            }
            if (result == 3) {
                processSynced = sync_process_blacklist(username, monitors);
                lastProcessFetch = GetTickCount();
                monitors.refresh();
            }
            if (result == 2 && send_policy_heartbeat(&subscription_tls) <= 0) break;
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
    return stop_requested;
}

/**
 * @brief Wait in one-second increments while polling the service stop event.
 * @return 1 if stop is observed, 0 after at least total_ms milliseconds of sleeping.
 */
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

/**
 * @brief Poll effective firewall state to surface changes made outside this backend.
 * Runs until the service stop wait completes; context is unused and return value is zero.
 */
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

/** @brief Log a Windows notification delivery failure reported by the notification helper. */
static void notification_failed(DWORD error) {
    LOGF("[Notifications] Windows toast delivery failed: 0x%08lX", error);
}

int DaemonRun(const char*) {
    bb_notifications_set_error_handler(notification_failed);
    HANDLE notification_thread = CreateThread(NULL, 0, notification_monitor, NULL, 0, NULL);
    if (!notification_thread) LOGF("[Notifications] Could not start firewall state monitor");
    char username[128] = {};
    int consecutive_failures = 0;
    int local_ready = (PingDaemon() == 0);

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

        if (is_server_connection_paused()) {
            sleep_with_stop_check(1000);
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
        LOGF("[Daemon] Server session ended%s", is_server_connection_paused() ? "; automatic reconnect paused" : "; retrying");
        SetServerSessionActive(0); // syncs filtration off when connection-sync is enabled
        block_all_if_no_fallback();
        if (is_server_connection_paused()) continue;

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

    if (notification_thread) {
        WaitForSingleObject(notification_thread, INFINITE);
        CloseHandle(notification_thread);
    }
    return 0;
}
