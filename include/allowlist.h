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

/** @brief Container for allowed IP addresses. */
typedef struct {
    AllowedIp ips[MAX_ALLOWED_IPS];
    size_t count;
} IpAllowlist;

/** @brief Initialize a whitelist structure. */
void Whitelist_Init(Whitelist* wl);

/** @brief Add a domain to the whitelist. */
int Whitelist_Add(Whitelist* wl, const char* domain);

/** @brief Check if a domain is in the whitelist. */
int Whitelist_Contains(Whitelist* wl, const char* domain);

/** @brief Initialize an IP allowlist structure. */
void IpAllowlist_Init(IpAllowlist* al);

/** @brief Remove expired entries from the IP allowlist. */
void IpAllowlist_Cleanup(IpAllowlist* al);

/** @brief Add an IP address to the allowlist with TTL. */
int IpAllowlist_Add(IpAllowlist* al, uint32_t ip, const char* domain, uint32_t ttl);

/** @brief Check if an IP address is in the allowlist. */
int IpAllowlist_Contains(IpAllowlist* al, uint32_t ip);

/** @brief Get domain associated with an IP address from allowlist. */
const char* IpAllowlist_GetDomain(IpAllowlist* al, uint32_t ip);

#endif
