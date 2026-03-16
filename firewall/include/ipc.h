/**
 * @file ipc.h
 * @brief Named pipe IPC server for BigBrother daemon.
 */

#ifndef IPC_H
#define IPC_H

#include <stdint.h>
#include <stdbool.h>

#define IPC_PIPE_NAME "BigBrother Daemon"
#define IPC_BUFFER_SIZE 4096

/**
 * @brief Start the IPC server in a separate thread.
 *
 * @return 0 on success, -1 on failure.
 */
int IpcStart(void);

/**
 * @brief Stop the IPC server.
 */
void IpcStop(void);

/**
 * @brief Reload the whitelist.
 *
 * @return 0 on success, error code on failure.
 */
int IpcReloadWhitelist(void);

/**
 * @brief Set the whitelist from a string.
 *
 * @param data Whitelist data (domains one per line).
 * @param size Size of data in bytes.
 *
 * @return 0 on success, error code on failure.
 */
int IpcSetWhitelist(const char* data, size_t size);

#endif // IPC_H
