#pragma once

#include "app.h"
#ifdef __MINGW32__
// MinGW aliases boolean and BYTE, duplicating an unused IReference specialization.
#define ____FIReference_1_boolean_INTERFACE_DEFINED__
#endif
#include <windows.ui.notifications.h>
#include <windows.data.xml.dom.h>

namespace notifications {
void submit_toast(ABI::Windows::UI::Notifications::IToastNotifier* notifier,
                  ABI::Windows::UI::Notifications::IToastNotification* toast);
}
