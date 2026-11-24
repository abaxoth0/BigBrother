/**
 * @file allowlist.h
 * @brief Domain whitelist and IP allowlist management.
 *
 * IPv4 values use host byte order. These containers have no internal locking:
 * callers synchronize access and keep borrowed results protected while in use.
 * Zero-initialize Whitelist before its first use. Mutations may invalidate pointers.
 */

#ifndef ALLOWLIST_H
#define ALLOWLIST_H

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "uthash.h"

#define MAX_DOMAIN_LEN 256
#define RULE_ANY_PORT (-2)
#define RULE_NO_PORT (-1)

#define WHITELIST_INITIAL_CAPACITY 1000

// Sweep expired IP allowlist entries at most this often (seconds).
#define IP_ALLOWLIST_SWEEP_INTERVAL 60

/** @brief Inclusive destination port range (0–65535). */
typedef struct {
    uint16_t first, last;
} PortRange;

/** @brief Single whitelist entry containing a host and optional ports. */
typedef struct {
    char* text;
    int32_t priority;
    PortRange* ports;
    size_t port_count;
    uint32_t ip;
    int is_ip;
    char domain[MAX_DOMAIN_LEN];
    // Lowercased copy of `domain`, precomputed at load time so the packet/DNS
    // hot paths never re-lowercase the pattern.
    char pattern_lower[MAX_DOMAIN_LEN];
    time_t added_time;
} WhitelistEntry;

/** @brief Priority-descending array of whitelist or exclusion rules. */
typedef struct {
    WhitelistEntry* entries;
    size_t count;
    size_t capacity;
} Whitelist;

/** @brief Owned lowercase DNS name with an absolute expiry time and next link. */
typedef struct CachedDomain {
    char domain[MAX_DOMAIN_LEN];
    time_t expires;
    struct CachedDomain* next;
} CachedDomain;

/** @brief Single allowed IP entry with associated domains and TTLs. */
typedef struct {
    uint32_t ip;                // hash key
    CachedDomain* domains;
    UT_hash_handle hh;          // uthash handle (must be last)
} AllowedIp;

/** @brief Container for allowed IP addresses (uthash table). */
typedef struct {
    AllowedIp* head;            // uthash table head
    size_t count;               ///< Stored IP count; may include entries awaiting expiry cleanup.
    time_t last_sweep;          // last time expired entries were purged
} IpAllowlist;

/** @brief Reset a zero-initialized or previously valid list, releasing existing entries. */
void WhitelistInit(Whitelist* wl);

/**
 * @brief Parse and insert one rule, including optional priority and port constraints.
 * Maintains descending priority order and ignores case-insensitive text duplicates.
 * @return 0 on success/duplicate, -1 on invalid syntax or allocation failure.
 */
int WhitelistAdd(Whitelist* wl, const char* domain);

/** @brief Clear all entries from the whitelist. */
void WhitelistClear(Whitelist* wl);

/** @brief Free the dynamic array backing a whitelist. */
void WhitelistFree(Whitelist* wl);

/**
 * @brief Replace both rule lists from size bytes of newline-separated input.
 * '!' rules go to bl. A leading standalone [N] sets the default priority;
 * blank lines and lines beginning with '#' or ';' after whitespace are ignored.
 * @return 0 on success, -1 on invalid input/allocation failure, preserving both old lists.
 */
int WhitelistLoadFromData(Whitelist* wl, Whitelist* bl, const char* data, size_t size);

/** @brief Clear all entries from the IP allowlist. */
void IpAllowlistClear(IpAllowlist* al);

/** @brief Release all hash entries and their owned domain chains. */
void IpAllowlistFree(IpAllowlist* al);

/** @brief Initialize an empty cache; free an existing cache before reinitializing it. */
void IpAllowlistInit(IpAllowlist* al);

/** @brief Remove expired entries from the IP allowlist. */
void IpAllowlistCleanup(IpAllowlist* al);

/**
 * @brief Cache a domain for an IPv4 address, refreshing an existing name case-insensitively.
 * Names are stored lowercase; ttl is in seconds and clamped to a minimum of 300.
 * Periodically removes expired entries before insertion.
 * @return 0 on success, -1 on invalid input or allocation failure.
 */
int IpAllowlistAdd(IpAllowlist* al, uint32_t ip, const char* domain, uint32_t ttl);

/** @brief Check if an IP address is in the allowlist. */
int IpAllowlistContains(IpAllowlist* al, uint32_t ip);

/** @brief Borrow the first unexpired domain for an address, or return NULL if none remains. */
const char* IpAllowlistGetDomain(IpAllowlist* al, uint32_t ip);

/**
 * @brief Look up an IP entry without an extra time() call.
 *
 * Returns a borrowed entry with at least one unexpired domain, or NULL. Pass the cached
 * current time so the packet hot path calls time() once instead of per lookup.
 */
AllowedIp* IpAllowlistLookup(IpAllowlist* al, uint32_t ip, time_t now);

/** @brief Remove an IP address from the allowlist. Returns 1 if removed. */
int IpAllowlistRemove(IpAllowlist* al, uint32_t ip);

/**
 * @brief Remove allowlist entries whose learned domain no longer matches any
 * rule in `wl` or optional `other`. Caller must hold the exclusive allowlist lock.
 */
void IpAllowlistPurgeUnowned(IpAllowlist* al, const Whitelist* wl, const Whitelist* other);

/**
 * @brief Test a lowercase domain against any rule, optionally checking a destination port.
 * RULE_ANY_PORT ignores port restrictions; RULE_NO_PORT matches only unrestricted rules.
 * @return 1 if a rule matches, otherwise 0; priorities/exclusions are not resolved here.
 */
int WhitelistMatches(const Whitelist* wl, const char* domain, int port);
/**
 * @brief Resolve packet policy using descending priority, with exclusions winning ties.
 * Both lists must be sorted (as maintained by WhitelistAdd/WhitelistLoadFromData).
 * ip is host-order IPv4; port is 0–65535 or RULE_NO_PORT for portless traffic.
 * Both DNS caches are checked for names whose expiry exceeds now.
 * @return 1 if the highest matching rule allows traffic, otherwise 0.
 */
int WhitelistAllows(const Whitelist* wl, const Whitelist* bl, const IpAllowlist* al,
                    const IpAllowlist* blocked, uint32_t ip, int port, time_t now);

#endif
