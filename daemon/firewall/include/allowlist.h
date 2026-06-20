/** @file allowlist.h
 * @brief Domain whitelist and IP allowlist management.
 */

#ifndef ALLOWLIST_H
#define ALLOWLIST_H

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#define MAX_WHITELIST_DOMAINS 256
#define MAX_DOMAIN_LEN 256
#define MAX_ALLOWED_IPS 1024

/** @brief Single whitelist entry containing a domain name. */
typedef struct {
    char domain[MAX_DOMAIN_LEN];
    time_t added_time;
    int is_exception;
} WhitelistEntry;

/** @brief Container for whitelisted domains. */
typedef struct {
    WhitelistEntry entries[MAX_WHITELIST_DOMAINS];
    size_t count;
} Whitelist;

/** @brief Single allowed IP entry with associated domain and TTL. */
typedef struct {
    uint32_t ip;
    char domain[MAX_DOMAIN_LEN];
    time_t expires;
} AllowedIp;

#define IP_ALLOW_LIST_CLEANUP_COOLDOWN 60 // 1 min

/** @brief Container for allowed IP addresses. */
typedef struct {
    AllowedIp ips[MAX_ALLOWED_IPS];
    size_t count;
    time_t last_cleared_at;
} IpAllowlist;

/** @brief Initialize a whitelist structure. */
void WhitelistInit(Whitelist* wl);

/** @brief Add a domain to the whitelist. */
int WhitelistAdd(Whitelist* wl, const char* domain, int is_exception);

/** @brief Check if a domain is in the whitelist (non-exception entries only). */
int WhitelistContains(Whitelist* wl, const char* domain);

/** @brief Check if a domain matches any exception entry in the whitelist. */
int WhitelistContainsException(Whitelist* wl, const char* domain);

/** @brief Clear all entries from the whitelist. */
void WhitelistClear(Whitelist* wl);

/** @brief Load whitelist from data (e.g., received via IPC). */
int WhitelistLoadFromData(Whitelist* wl, const char* data, size_t size);

/** @brief Clear all entries from the IP allowlist. */
void IpAllowlistClear(IpAllowlist* al);

/** @brief Initialize an IP allowlist structure. */
void IpAllowlistInit(IpAllowlist* al);

/** @brief Remove expired entries from the IP allowlist. */
void IpAllowlistCleanup(IpAllowlist* al);

/** @brief Add an IP address to the allowlist with TTL. */
int IpAllowlistAdd(IpAllowlist* al, uint32_t ip, const char* domain, uint32_t ttl);

/** @brief Check if an IP address is in the allowlist. */
int IpAllowlistContains(IpAllowlist* al, uint32_t ip);

/** @brief Get domain associated with an IP address from allowlist. */
const char* IpAllowlistGetDomain(IpAllowlist* al, uint32_t ip);

#endif
