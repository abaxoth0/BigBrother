/**
 * @file ipc_daemon.h
 * @brief IPC client for connecting to BigBrother Daemon.
 */

#ifndef IPC_H
#define IPC_H

#include <stddef.h>

#define DAEMON_PIPE_NAME "BigBrother Daemon"
#define DAEMON_PIPE_BUFFER_SIZE 4096

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
 * @brief Connect to the daemon and reload whitelist from file.
 *
 * @param out_buffer Output buffer for response.
 * @param buffer_size Size of output buffer.
 *
 * @return 0 on success, -1 on error.
 */
int DaemonReload(char* out_buffer, size_t buffer_size);

/**
 * @brief Set the server IP address for daemon mode.
 *
 * @param ip Server IP address.
 */
void SetServerIp(const char* ip);

/**
 * @brief Check if running in daemon mode.
 *
 * @return Non-zero if in daemon mode.
 */
int IsDaemonMode(void);

/**
 * @brief Set daemon mode.
 *
 * @param mode Non-zero to enable daemon mode.
 */
void SetDaemonMode(int mode);

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

#endif // IPC_H
