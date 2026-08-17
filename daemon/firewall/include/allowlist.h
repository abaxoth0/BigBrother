/** @file allowlist.h
 * @brief Domain whitelist and IP allowlist management.
 */

#ifndef ALLOWLIST_H
#define ALLOWLIST_H

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#define MAX_DOMAIN_LEN 256

#define WHITELIST_INITIAL_CAPACITY 1000
#define IP_ALLOWLIST_INITIAL_CAPACITY 1000

/** @brief Single whitelist entry containing a domain name (allow rules only). */
typedef struct {
    char domain[MAX_DOMAIN_LEN];
    time_t added_time;
} WhitelistEntry;

/** @brief Container for whitelisted domains (allow rules). */
typedef struct {
    WhitelistEntry* entries;
    size_t count;
    size_t capacity;
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
    AllowedIp* ips;
    size_t count;
    size_t capacity;
    time_t last_cleared_at;
} IpAllowlist;

/** @brief Initialize a whitelist structure. */
void WhitelistInit(Whitelist* wl);

/** @brief Add a domain to the whitelist. */
int WhitelistAdd(Whitelist* wl, const char* domain);

/** @brief Check if a domain is in the whitelist. */
int WhitelistContains(Whitelist* wl, const char* domain);

/** @brief Clear all entries from the whitelist. */
void WhitelistClear(Whitelist* wl);

/** @brief Free the dynamic array backing a whitelist. */
void WhitelistFree(Whitelist* wl);

/** @brief Load whitelist from data (e.g., received via IPC); '!' entries go to bl. */
int WhitelistLoadFromData(Whitelist* wl, Whitelist* bl, const char* data, size_t size);

/** @brief Clear all entries from the IP allowlist. */
void IpAllowlistClear(IpAllowlist* al);

/** @brief Free the dynamic array backing an IP allowlist. */
void IpAllowlistFree(IpAllowlist* al);

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

/** @brief Remove an IP address from the allowlist. Returns 1 if removed. */
int IpAllowlistRemove(IpAllowlist* al, uint32_t ip);

#endif
