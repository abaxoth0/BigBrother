/**
 * @file ipc.h
 * @brief IPC client for connecting to BigBrother Daemon.
 */

#ifndef IPC_H
#define IPC_H

#include <stddef.h>

#define IPC_PIPE_NAME "BigBrother Daemon"
#define IPC_BUFFER_SIZE 4096

/**
 * @brief Connect to the daemon and get status.
 *
 * @param out_buffer Output buffer for response.
 * @param buffer_size Size of output buffer.
 *
 * @return 0 on success, -1 on error.
 */
int IpcGetStatus(char* out_buffer, size_t buffer_size);

/**
 * @brief Connect to the daemon and get whitelist.
 *
 * @param out_buffer Output buffer for response.
 * @param buffer_size Size of output buffer.
 *
 * @return 0 on success, -1 on error.
 */
int IpcGetWhitelist(char* out_buffer, size_t buffer_size);

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
int IpcSetWhitelist(const char* data, size_t size, char* out_buffer, size_t buffer_size);

/**
 * @brief Connect to the daemon and reload whitelist from file.
 *
 * @param out_buffer Output buffer for response.
 * @param buffer_size Size of output buffer.
 *
 * @return 0 on success, -1 on error.
 */
int IpcReload(char* out_buffer, size_t buffer_size);

#endif // IPC_H
