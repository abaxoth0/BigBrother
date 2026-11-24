/**
 * @file ipc_client.h
 * @brief Named pipe server for Client Frontend & Backend communication.
 */

#ifndef FRONTEND_SERVER_H
#define FRONTEND_SERVER_H

/**
 * @brief Create the shutdown event and launch the IPC listener thread.
 * @pre Called once by the backend lifecycle owner, before accepting requests.
 * @return 0 if the thread launched, -1 on event/thread creation failure.
 * Successful launch does not guarantee the listener has created its pipe yet.
 */
int StartClientServer(void);
/**
 * @brief Signal shutdown, join the listener, cancel worker I/O and wait for workers to exit.
 * Releases the listener, worker and shutdown-event handles.
 * @pre Called by the lifecycle owner, never from an IPC worker or listener thread.
 */
void StopClientServer(void);

/**
 * @brief Signal shutdown and wake a listener blocked in ConnectNamedPipe().
 * Does not join workers or release their handles; StopClientServer() completes shutdown.
 */
void SignalClientServerShutdown(void);

/**
 * @brief Initialize logging for the client backend.
 */
extern "C" void log_init(void);

/**
 * @brief Advance the state generation and wake registered event subscribers.
 * Safe for concurrent callers; subscriber handles are protected by the static SRW lock.
 */
void NotifyStateChanged(void);

#endif // FRONTEND_SERVER_H
