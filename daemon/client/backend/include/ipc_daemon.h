/**
 * @file ipc_daemon.h
 * @brief IPC client for connecting to BigBrother Daemon.
 */

#ifndef IPC_H
#define IPC_H

#include <stddef.h>
#include <stdint.h>
#include <windows.h>

#define DAEMON_PIPE_NAME "BigBrother.Firewall"
#define DAEMON_PIPE_BUFFER_SIZE 4096

#define DISCOVERY_MAX_SERVERS 32

extern uint32_t g_whitelist_revision;
extern HANDLE g_ServiceStopEvent;

/**
 * @brief Connect to the daemon and get status.
 *
 * @param out_buffer Output buffer for response.
 * @param buffer_size Size of output buffer.
 *
 * @return 0 on success, -1 on error.
 */
int DaemonGetStatus(char* out_buffer, size_t buffer_size);

/**
 * @brief Connect to the daemon and get whitelist.
 *
 * @param out_buffer Output buffer for response.
 * @param buffer_size Size of output buffer.
 *
 * @return 0 on success, -1 on error.
 */
int DaemonGetWhitelist(char* out_buffer, size_t buffer_size);

/**
 * @brief Connect to the daemon and set whitelist.
 *
 * @param data Whitelist data (domains one per line).
 * @param size Size of data in bytes.
 * @param out_buffer Output buffer for response.
 * @param buffer_size Size of output buffer.
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

/**
 * @brief Set the active session state with the server.
 */
void SetServerSessionActive(int active);

/**
 * @brief Check if there is an active user session with the server.
 *
 * @return 1 if connected, 0 otherwise.
 */
int IsServerSessionActive(void);

/**
 * @brief Get the currently configured server IP address.
 *
 * @return Pointer to static server IP string, or empty string if not set.
 */
const char* GetServerIp(void);

/**
 * @brief Set the server IP address for daemon mode.
 *
 * @param ip Server IP address.
 */
void SetServerIp(const char* ip);

/**
 * @brief Load server IP from config file.
 */
void load_server_ip(void);

/**
 * @brief Run in daemon mode - poll server for whitelist updates.
 *
 * @param server_ip Server IP address.
 * @param poll_interval_secs Polling interval in seconds.
 *
 * @return 0 on exit.
 */
int DaemonRun(const char* server_ip, int poll_interval_secs);

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
 * @param out Output buffer.
 * @param out_size Buffer size.
 *
 * @return 1 if found, 0 if not found.
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
 * @return 0 on success, -1 on error.
 */
int ServerRegister(const char* name);

/**
 * @brief Connect user to server.
 *
 * @param name Username to connect.
 *
 * @return 0 on success, -1 on error.
 */
int ServerConnect(const char* name);

/**
 * @brief Disconnect user from server.
 *
 * @param name Username to disconnect.
 *
 * @return 0 on success, -1 on error.
 */
int ServerDisconnect(const char* name);

/**
 * @brief Refresh connection TTL on server.
 *
 * @param name Username to refresh.
 *
 * @return 0 on success, -1 on error.
 */
int ServerRefresh(const char* name);

/**
 * @brief Change username on server.
 *
 * @param oldName Current username.
 * @param newName New username.
 *
 * @return 0 on success, -1 on error.
 */
int ServerChangeName(const char* oldName, const char* newName);

#endif // IPC_H
