#include "../include/allowlist.h"
#include "../include/common.h"
#include <stdio.h>

// Grow a dynamic array (same realloc-doubling pattern as DA_GROW in common.h)
// to make room for at least `count + n` elements. Returns 0 on success, -1 on OOM.
static int grow(void** elems, size_t* cap, size_t elem_size, size_t count, size_t n, size_t initial_cap) {
    if (count + n <= *cap) return 0;
    size_t new_cap = *cap > 0 ? *cap : initial_cap;
    while (count + n > new_cap) {
        if (new_cap > SIZE_MAX / 2) return -1;
        new_cap *= 2;
    }
    void* tmp = realloc(*elems, new_cap * elem_size);
    if (!tmp) return -1;
    *elems = tmp;
    *cap = new_cap;
    return 0;
}

// Match domain against whitelist patterns.
// Supports wildcard suffix patterns like "*.example.com".
static int match_domain(const char* domain, const char* pattern) {
    char domain_lower[MAX_DOMAIN_LEN];
    char pattern_lower[MAX_DOMAIN_LEN];

    strncpy(domain_lower, domain, MAX_DOMAIN_LEN - 1);
    domain_lower[MAX_DOMAIN_LEN - 1] = '\0';
    ToLowerInplace(domain_lower);

    strncpy(pattern_lower, pattern, MAX_DOMAIN_LEN - 1);
    pattern_lower[MAX_DOMAIN_LEN - 1] = '\0';
    ToLowerInplace(pattern_lower);

    if (pattern_lower[0] == '*' && pattern_lower[1] == '.') {
        const char* suffix = pattern_lower + 2;
        size_t suffix_len = strlen(suffix);
        size_t domain_len = strlen(domain_lower);

        if (domain_len >= suffix_len) {
            const char* pos = domain_lower + domain_len - suffix_len;
            if (strcmp(pos, suffix) == 0 &&
                (domain_len == suffix_len || *(pos - 1) == '.')) {
                return 1;
            }
        }
        return 0;
    }

    return strcmp(domain_lower, pattern_lower) == 0;
}

void WhitelistInit(Whitelist* wl) {
    if (!wl) return;
    WhitelistFree(wl);
    wl->capacity = WHITELIST_INITIAL_CAPACITY;
    wl->entries = calloc(wl->capacity, sizeof(WhitelistEntry));
    if (!wl->entries) {
        wl->capacity = 0;
    }
}

int WhitelistAdd(Whitelist* wl, const char* domain) {
    if (!wl || !domain) {
        return -1;
    }
    for (size_t i = 0; i < wl->count; i++) {
        if (strcmp(wl->entries[i].domain, domain) == 0) {
            return 0;
        }
    }
    if (grow((void**)&wl->entries, &wl->capacity, sizeof(WhitelistEntry), wl->count, 1, WHITELIST_INITIAL_CAPACITY) != 0) {
        return -1;
    }
    strncpy(wl->entries[wl->count].domain, domain, MAX_DOMAIN_LEN - 1);
    wl->entries[wl->count].domain[MAX_DOMAIN_LEN - 1] = '\0';
    wl->entries[wl->count].added_time = time(NULL);
    wl->count++;
    return 0;
}

int WhitelistContains(Whitelist* wl, const char* domain) {
    if (!wl || !domain) return 0;
    for (size_t i = 0; i < wl->count; i++) {
        if (match_domain(domain, wl->entries[i].domain)) {
            return 1;
        }
    }
    return 0;
}

void WhitelistClear(Whitelist* wl) {
    if (!wl) return;
    wl->count = 0;
}

void WhitelistFree(Whitelist* wl) {
    if (!wl) return;
    free(wl->entries);
    wl->entries = NULL;
    wl->count = 0;
    wl->capacity = 0;
}

int WhitelistLoadFromData(Whitelist* wl, Whitelist* bl, const char* data, size_t size) {
    if (!wl || !bl || !data || size == 0) return -1;

    WhitelistClear(wl);
    WhitelistClear(bl);

    char* copy = malloc(size + 1);
    if (!copy) return -1;
    memcpy(copy, data, size);
    copy[size] = '\0';

    // Split on newlines without strtok (strtok uses static state and races when
    // multiple IPC handlers run concurrently).
    char* line = copy;
    while (line) {
        char* next = strchr(line, '\n');
        if (next) *next = '\0';

        while (*line == ' ' || *line == '\r') line++;
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }

        if (len > 0 && line[0] != '#') {
            int is_exception = (line[0] == '!');
            const char* domain = is_exception ? line + 1 : line;
            WhitelistAdd(is_exception ? bl : wl, domain);
        }

        line = next ? next + 1 : NULL;
    }

    free(copy);
    return 0;
}

void IpAllowlistClear(IpAllowlist* al) {
    if (!al) return;
    al->count = 0;
}

void IpAllowlistFree(IpAllowlist* al) {
    if (!al) return;
    free(al->ips);
    al->ips = NULL;
    al->count = 0;
    al->capacity = 0;
}

void IpAllowlistInit(IpAllowlist* al) {
    if (!al) return;
    IpAllowlistFree(al);
    al->capacity = IP_ALLOWLIST_INITIAL_CAPACITY;
    al->ips = calloc(al->capacity, sizeof(AllowedIp));
    if (!al->ips) {
        al->capacity = 0;
    }
}

void IpAllowlistCleanup(IpAllowlist* al) {
    if (!al) return;
    time_t now = time(NULL);
    size_t valid_count = 0;
    for (size_t i = 0; i < al->count; i++) {
        if (al->ips[i].expires > now) {
            if (valid_count != i) {
                al->ips[valid_count] = al->ips[i];
            }
            valid_count++;
        }
    }
    al->count = valid_count;
}

int IpAllowlistAdd(IpAllowlist* al, uint32_t ip, const char* domain, uint32_t ttl) {
    if (!al) return -1;

    IpAllowlistCleanup(al);

    if (grow((void**)&al->ips, &al->capacity, sizeof(AllowedIp), al->count, 1, IP_ALLOWLIST_INITIAL_CAPACITY) != 0) {
        // Force cleanup and retry once before giving up
        al->last_cleared_at = 0;
        IpAllowlistCleanup(al);
        if (grow((void**)&al->ips, &al->capacity, sizeof(AllowedIp), al->count, 1, IP_ALLOWLIST_INITIAL_CAPACITY) != 0) {
            return -1;
        }
    }

    // Enforce a minimum TTL of 5 minutes to prevent rapid expiry
    // of IPs from CDN domains (many use 60s or shorter TTLs).
    if (ttl < 300) ttl = 300;

    for (size_t i = 0; i < al->count; i++) {
        if (al->ips[i].ip == ip) {
            al->ips[i].expires = time(NULL) + ttl;
            if (domain) {
                strncpy(al->ips[i].domain, domain, MAX_DOMAIN_LEN - 1);
            }
            return 0;
        }
    }

    al->ips[al->count].ip = ip;
    if (domain) {
        strncpy(al->ips[al->count].domain, domain, MAX_DOMAIN_LEN - 1);
        al->ips[al->count].domain[MAX_DOMAIN_LEN - 1] = '\0';
    } else {
        al->ips[al->count].domain[0] = '\0';
    }
    al->ips[al->count].expires = time(NULL) + ttl;
    al->count++;

    return 0;
}

int IpAllowlistContains(IpAllowlist* al, uint32_t ip) {
    if (!al) return 0;
    time_t now = time(NULL);
    // Pure read: check expiry inline without mutating the array (no cleanup),
    // so this is safe to call from the packet thread under the shared lock.
    for (size_t i = 0; i < al->count; i++) {
        if (al->ips[i].ip == ip && al->ips[i].expires > now) {
            return 1;
        }
    }
    return 0;
}

const char* IpAllowlistGetDomain(IpAllowlist* al, uint32_t ip) {
    if (!al) return NULL;
    time_t now = time(NULL);
    // Pure read: expired entries are treated as absent (cleanup is done by the
    // writer under the exclusive lock).
    for (size_t i = 0; i < al->count; i++) {
        if (al->ips[i].ip == ip && al->ips[i].expires > now) {
            return al->ips[i].domain;
        }
    }
    return NULL;
}

int IpAllowlistRemove(IpAllowlist* al, uint32_t ip) {
    if (!al) return 0;
    for (size_t i = 0; i < al->count; i++) {
        if (al->ips[i].ip == ip) {
            al->ips[i] = al->ips[al->count - 1];
            al->count--;
            return 1;
        }
    }
    return 0;
}
