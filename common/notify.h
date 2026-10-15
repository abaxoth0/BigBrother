#ifndef BB_NOTIFY_H
#define BB_NOTIFY_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*bb_notification_error_handler)(DWORD error);

void bb_notifications_set_error_handler(bb_notification_error_handler handler);
/* Text is UTF-8; delivery failures after launch reach the error handler. */
int bb_notify(const char* title, const char* message);

#ifdef __cplusplus
}
#endif

#endif
