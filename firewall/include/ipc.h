/**
 * @file ipc.h
 * @brief Named pipe IPC server for BigBrother daemon.
 */

#ifndef IPC_H
#define IPC_H

#include <stdint.h>
#include <stdbool.h>
#include <windows.h>

#define IPC_PIPE_NAME "BigBrother.Firewall"
#define IPC_BUFFER_SIZE 4096
// Max IPC message size: large enough for a full whitelist (256 domains x 256 chars).
#define IPC_MAX_MESSAGE_SIZE (64 * 1024)

/**
 * @brief Start the IPC server in a separate thread.
 *
 * @param stop_event Event to signal for shutdown. If NULL, uses internal event.
 * @return 0 on success, -1 on failure.
 */
int IpcStart(HANDLE stop_event);

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
