/**
 * @file ipc_client.h
 * @brief Named pipe server for Client Frontend & Backend communication.
 */

#ifndef FRONTEND_SERVER_H
#define FRONTEND_SERVER_H

/**
 * @brief Start client IPC server in a background thread.
 */
int StartClientServer(void);
void StopClientServer(void);

/**
 * @brief Signal the client server to shut down.
 */
void SignalClientServerShutdown(void);

/**
 * @brief Initialize logging for the client backend.
 */
extern "C" void log_init(void);

/**
 * @brief Notify subscribers that daemon state has changed.
 */
void NotifyStateChanged(void);

#endif // FRONTEND_SERVER_H
