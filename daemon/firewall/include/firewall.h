#ifndef FIREWALL_H
#define FIREWALL_H

#include "allowlist.h"
#include <windows.h>

extern Whitelist g_Whitelist;
extern IpAllowlist g_IpAllowlist;
extern HANDLE g_ServiceStopEvent;

int LoadWhiteList(char* path);
void PreResolveWhitelist(void);

#endif
