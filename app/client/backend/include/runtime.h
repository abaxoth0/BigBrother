/**
 * @file runtime.h
 * @brief Backend service restart helpers.
 */

#pragma once

/**
 * @brief Launch a helper that restarts the service after this process exits.
 * @return true if the helper launched; this does not confirm the later restart.
 */
bool RequestBackendRestart();
/**
 * @brief Wait for the supplied decimal process ID to exit, then start the backend service.
 * @return 0 if the start request succeeds, 1 on invalid input, timeout or service failure.
 */
int RestartBackendService(const char* process_id);
