/**
 * @file notify.h
 * @brief Asynchronous Windows notification delivery.
 */

#ifndef BB_NOTIFY_H
#define BB_NOTIFY_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Asynchronous delivery-failure callback; invoked on a background thread. */
typedef void (*bb_notification_error_handler)(DWORD error);

/** @brief Replace the process-wide delivery error callback; NULL restores debug output. */
void bb_notifications_set_error_handler(bb_notification_error_handler handler);
/**
 * @brief Queue a Client notification using UTF-8 title/message text.
 * @return 0 when delivery is queued, -1 on launch/validation failure (GetLastError describes it).
 * Later delivery failures reach the registered callback on a background thread.
 */
int bb_notify(const char* title, const char* message);

#ifdef __cplusplus
}
#endif

#endif
