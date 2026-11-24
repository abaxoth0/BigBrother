/**
 * @file firewall.h
 * @brief Firewall policy state and file loading.
 */

#ifndef FIREWALL_H
#define FIREWALL_H

#include "allowlist.h"
#include <windows.h>

extern Whitelist g_Whitelist;
extern Whitelist g_Blacklist;
extern IpAllowlist g_IpAllowlist;
extern IpAllowlist g_IpBlocklist;
extern HANDLE g_ServiceStopEvent;
extern SRWLOCK g_AllowlistLock;
extern int g_FiltrationEnabled;
extern volatile LONG g_BlockIpv6;

/**
 * @brief Load a file into the active policy; NULL selects the configured default path.
 * Call during startup or while holding the exclusive allowlist lock. An unreadable
 * file clears the policy and attempts to create an empty file.
 * @return STATUS_OK or STATUS_FAILED_TO_READ_WHITELIST on read/parse/allocation failure.
 */
int LoadWhiteList(char* path);
/** @brief Resolve rule domains to warm the address caches before packet processing. */
void PreResolveWhitelist(void);

#endif
