/**
 * @file ipc_daemon.h
 * @brief Firewall requests, persisted client settings and remote server session management.
 */

#ifndef IPC_H
#define IPC_H

#include <stddef.h>
#include <stdint.h>
#include <windows.h>

#define DAEMON_PIPE_NAME "BigBrother.Firewall"
#define DAEMON_PIPE_BUFFER_SIZE 4096
// Soft cap for a full whitelist + exception rules dump. Message-mode named
// pipes bound an individual message by their buffer size, so this is a large
// safety bound rather than a strict limit — realistically unlimited. Keep in
// sync with the firewall's IPC_MAX_MESSAGE_SIZE.
#define DAEMON_MAX_MESSAGE_SIZE (16 * 1024 * 1024)

#define DISCOVERY_MAX_SERVERS 32

extern uint32_t g_whitelist_revision;
extern HANDLE g_ServiceStopEvent;

/** @brief Invalidate cached firewall policy state so the next sync reapplies it. */
void ResetFirewallSync(void);

/**
 * @brief Connect to the daemon and get status.
 *
 * @param out_buffer Output buffer for response.
 * @param buffer_size Output capacity in bytes, including the trailing NUL.
 *
 * @return 0 on success, -1 on error.
 */
int DaemonGetStatus(char* out_buffer, size_t buffer_size);

/**
 * @brief Connect to the daemon and get whitelist.
 *
 * @param out_buffer Output buffer for response.
 * @param buffer_size Output capacity in bytes, including the trailing NUL.
 *
 * @return 0 on success, -1 on error.
 */
int DaemonGetWhitelist(char* out_buffer, size_t buffer_size);

/**
 * @brief Connect to the daemon and set whitelist.
 *
 * @param data NUL-terminated whitelist rules, one per line; embedded NULs are unsupported.
 * @param size Legacy argument retained for callers; the implementation uses strlen(data).
 * @param out_buffer Output buffer for response.
 * @param buffer_size Output capacity in bytes, including the trailing NUL.
 *
 * @return 0 on success, -1 on error.
 */
int DaemonSetWhitelist(const char* data, size_t size, char* out_buffer, size_t buffer_size);

/**
 * @brief Reload whitelist on daemon.
 *
 * @param out_buffer Output buffer for response.
 * @param buffer_size Buffer size.
 *
 * @return 0 on success, -1 on failure.
 */
int DaemonReloadWhitelist(char* out_buffer, size_t buffer_size);

/**
 * @brief Check if a server IP is configured.
 *
 * @return 1 if configured, 0 otherwise.
 */
int HasServerIp(void);
/** @brief Restrict persisted client secrets to administrators/SYSTEM; return 0 or -1 on failure. */
int PrepareClientSecretPermissions(void);

/**
 * @brief Snapshot configured address, port and pin, and probe the observed certificate.
 * @return 0 on successful inspection, -1 when configuration or probing fails.
 */
int InspectServerCertificate(char address[64], int* port, char pinned[65], char observed[65]);
/**
 * @brief Save a verified pin only if address, port and original pin still match the configuration.
 * @return 0 on success, -1 on invalid input, concurrent configuration change or persistence failure.
 */
int UpdateServerFingerprint(const char* address, int port, const char* original_pin,
                            const char* verified_fingerprint);

/**
 * @brief Set session state, notify on transitions and reconcile effective filtration.
 * A paused connection cannot be marked active until ResumeServerConnection() is called.
 */
void SetServerSessionActive(int active);
/** @brief Clear manual disconnect state and wake the connection loop. */
void ResumeServerConnection(void);

/**
 * @brief Check if there is an active user session with the server.
 *
 * @return 1 if connected, 0 otherwise.
 */
int IsServerSessionActive(void);

/**
 * @brief Get the currently configured server IP address.
 *
 * @return Borrowed thread-local snapshot, empty when unset; overwritten by the next call
 * on the same thread and invalid after that thread exits. Copy it for longer-lived use.
 */
const char* GetServerIp(void);

/**
 * @brief Update the in-memory server address and attempt to persist it under the config lock.
 *
 * @param ip NUL-terminated address, or NULL for no change. Persistence errors are not returned.
 */
void SetServerIp(const char* ip);

/**
 * @brief Load server IP from config file.
 */
void load_server_ip(void);

/**
 * @brief Run in daemon mode - event-driven subscription to server updates.
 *
 * @param server_ip Legacy argument, currently unused; configure the address with SetServerIp().
 *
 * @return 0 on exit.
 */
int DaemonRun(const char* server_ip);

/**
 * @brief Ping the local daemon to check if it's alive.
 *
 * @return 0 if alive, -1 if not responding.
 */
int PingDaemon(void);

/**
 * @brief Ping the remote server to check connection.
 *
 * @return 0 if reachable, -1 if not responding.
 */
int PingServer(void);

/**
 * @brief Get filtration enabled status from daemon.
 *
 * @return 1 if the firewall reports enabled, 0 if disabled or unreachable.
 */
int DaemonGetFiltration(void);

/**
 * @brief Set filtration enabled status on daemon.
 *
 * @param enabled 1 to enable filtration, 0 to disable.
 *
 * @return 0 on success, -1 on error.
 */
int DaemonSetFiltration(int enabled);

/**
 * @brief Get local filtration enabled status (cached).
 *
 * @return 1 if filtration is enabled, 0 if disabled.
 */
int IsFiltrationEnabled(void);

/** IPv6 blocking preference, persisted in config.ini (enabled by default). */
int IsIpv6BlockEnabled(void);
/**
 * @brief Persist the IPv6 preference and request serialized firewall synchronization.
 * @return 0 after saving, even if the firewall is unavailable; -1 if persistence fails.
 * Startup/status reconciliation retries a saved preference that could not yet be applied.
 */
int SetIpv6BlockEnabled(int enabled);

/** @brief Return the persisted Windows notification preference (enabled by default). */
int IsWindowsNotificationsEnabled(void);
/** @brief Persist the notification preference; return 0 on success, -1 on failure. */
int SetWindowsNotificationsEnabled(int enabled);

/**
 * @brief Recompute and return the effective (connection-gated) filtration state.
 *
 * Called when status is reported so the value always reflects the current
 * server connection state; pushes the updated state to the firewall if needed.
 *
 * @return 1 if filtration is effective, 0 if disabled.
 */
int RefreshFiltrationEffective(void);

/**
 * @brief Check if filtration is synced with the server connection (enabled by default).
 *
 * When enabled, filtration is only effective while connected to the server; it
 * is automatically disabled whenever the connection is down (whatever the
 * reason) and re-enabled on reconnect, unless the user turned it off manually.
 *
 * @return 1 if enabled, 0 if disabled.
 */
int IsFiltrationAutoDisableEnabled(void);

/**
 * @brief Enable or disable filtration connection-sync.
 *
 * @param enabled 1 to enable, 0 to disable.
 */
void SetFiltrationAutoDisableEnabled(int enabled);

/**
 * @brief Enable or disable fallback whitelist (use local file when server sync is unavailable).
 *
 * @param enabled 1 to enable fallback, 0 to disable (block all traffic).
 */
void SetFallbackWhitelistEnabled(int enabled);

/**
 * @brief Check if fallback whitelist is enabled.
 *
 * @return 1 if enabled, 0 if disabled.
 */
int IsFallbackWhitelistEnabled(void);

/**
 * @brief Read a string value from config.ini.
 *
 * @param section Section name.
 * @param key Key name.
 * @param out Output buffer, NUL-terminated when non-null and out_size is positive.
 * @param out_size Buffer capacity including NUL; long values are truncated.
 *
 * @return 1 if found, 0 if missing, unreadable or given an invalid output buffer.
 * Serializes file access and may recover misplaced legacy credentials. An ambiguous
 * or invalid credential is returned as a nonempty rejection marker, not a missing key.
 */
int ini_get_string(const char* section, const char* key, char* out, size_t out_size);

/**
 * @brief Write or update a string value in config.ini.
 *
 * @param section Section name.
 * @param key Key name.
 * @param value Value to write.
 *
 * @return 0 on success, -1 on error.
 */
int ini_set_string(const char* section, const char* key, const char* value);

/**
 * @brief Load username from config file.
 *
 * @param buffer Output buffer for username.
 * @param size Buffer size.
 *
 * @return 0 on success, -1 if not found or error.
 */
int LoadUserName(char* buffer, size_t size);

/**
 * @brief Save username to config file.
 *
 * @param name Username to save.
 *
 * @return 0 on success, -1 on error.
 */
int SaveUserName(const char* name);

/**
 * @brief Register user on server (pending approval).
 *
 * @param name Username to register.
 *
 * @return 0 on success, -1 on request failure or -2 for an explicit invalid-token rejection.
 */
int ServerRegister(const char* name);

/**
 * @brief Connect user to server.
 *
 * @param name Username to connect.
 *
 * @return 0 on success, -1 on request failure or -2 for an explicit invalid-token rejection.
 */
int ServerConnect(const char* name);

/**
 * @brief Disconnect user from server.
 *
 * @param name Username to disconnect.
 *
 * @return 0 on success, -1 on request failure or -2 for an explicit invalid-token rejection.
 */
int ServerDisconnect(const char* name);

/**
 * @brief Refresh connection TTL on server.
 *
 * @param name Username to refresh.
 *
 * @return 0 on success, -1 on request failure or -2 for an explicit invalid-token rejection.
 */
int ServerRefresh(const char* name);

/**
 * @brief Change username on server.
 *
 * @param oldName Current username.
 * @param newName New username.
 *
 * @return 0 on success, -1 on request failure or -2 for an explicit invalid-token rejection.
 */
int ServerChangeName(const char* oldName, const char* newName);

/**
 * @brief Request update metadata from the server into a NUL-terminated buffer.
 * @return 0 for an OK reply, SERVER_INVALID_TOKEN (-2) if token recovery is rejected again,
 * or -1 on other request/credential failures. The current transport may truncate a reply
 * to fit size; success alone does not guarantee complete metadata.
 */
int ServerGetUpdate(char* output, size_t size);

#endif // IPC_H
