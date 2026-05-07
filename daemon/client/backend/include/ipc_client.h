/**
 * @file ipc_client.h
 * @brief Named pipe server for Client Frontend & Backend communication.
 */

#ifndef FRONTEND_SERVER_H
#define FRONTEND_SERVER_H

/**
 * @brief Start client IPC server in a background thread.
 */
void StartClientServer(void);

/**
 * @brief Signal the client server to shut down.
 */
void SignalClientServerShutdown(void);

/**
 * @brief Initialize logging for the client backend.
 */
void log_init(void);

#endif // FRONTEND_SERVER_H
