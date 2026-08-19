/** @file allowlist.h
 * @brief Domain whitelist and IP allowlist management.
 */

#ifndef ALLOWLIST_H
#define ALLOWLIST_H

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "uthash.h"

#define MAX_DOMAIN_LEN 256

#define WHITELIST_INITIAL_CAPACITY 1000

// Sweep expired IP allowlist entries at most this often (seconds).
#define IP_ALLOWLIST_SWEEP_INTERVAL 60

/** @brief Single whitelist entry containing a domain name (allow rules only). */
typedef struct {
    char domain[MAX_DOMAIN_LEN];
    // Lowercased copy of `domain`, precomputed at load time so the packet/DNS
    // hot paths never re-lowercase the pattern.
    char pattern_lower[MAX_DOMAIN_LEN];
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
    uint32_t ip;                // hash key
    char domain[MAX_DOMAIN_LEN];
    time_t expires;
    UT_hash_handle hh;          // uthash handle (must be last)
} AllowedIp;

/** @brief Container for allowed IP addresses (uthash table). */
typedef struct {
    AllowedIp* head;            // uthash table head
    size_t count;               // live (unexpired) entry count
    time_t last_sweep;          // last time expired entries were purged
} IpAllowlist;

/** @brief Initialize a whitelist structure. */
void WhitelistInit(Whitelist* wl);

/** @brief Add a domain to the whitelist. */
int WhitelistAdd(Whitelist* wl, const char* domain);

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

/**
 * @brief Look up an IP entry without an extra time() call.
 *
 * Returns the live (unexpired) entry for `ip`, or NULL. Pass the cached
 * current time so the packet hot path calls time() once instead of per lookup.
 */
AllowedIp* IpAllowlistLookup(IpAllowlist* al, uint32_t ip, time_t now);

/** @brief Remove an IP address from the allowlist. Returns 1 if removed. */
int IpAllowlistRemove(IpAllowlist* al, uint32_t ip);

/**
 * @brief Remove allowlist entries whose learned domain no longer matches any
 * allow rule in `wl`. Caller must hold the exclusive allowlist lock.
 */
void IpAllowlistPurgeUnowned(IpAllowlist* al, const Whitelist* wl);

#endif
