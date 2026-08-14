#ifndef FIREWALL_H
#define FIREWALL_H

#include "allowlist.h"
#include <windows.h>

extern Whitelist g_Whitelist;
extern IpAllowlist g_IpAllowlist;
extern IpAllowlist g_IpBlocklist;
extern HANDLE g_ServiceStopEvent;
extern SRWLOCK g_AllowlistLock;
extern int g_FiltrationEnabled;

int LoadWhiteList(char* path);
void PreResolveWhitelist(void);

#endif
