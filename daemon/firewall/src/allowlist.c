#include "../include/allowlist.h"
#include "../include/common.h"
#include <stdio.h>

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
    memset(wl, 0, sizeof(Whitelist));
}

int WhitelistAdd(Whitelist* wl, const char* domain, int is_exception) {
    if (!wl || !domain || wl->count >= MAX_WHITELIST_DOMAINS) {
        return -1;
    }
    for (size_t i = 0; i < wl->count; i++) {
        if (strcmp(wl->entries[i].domain, domain) == 0 &&
            wl->entries[i].is_exception == is_exception) {
            return 0;
        }
    }
    strncpy(wl->entries[wl->count].domain, domain, MAX_DOMAIN_LEN - 1);
    wl->entries[wl->count].domain[MAX_DOMAIN_LEN - 1] = '\0';
    wl->entries[wl->count].added_time = time(NULL);
    wl->entries[wl->count].is_exception = is_exception;
    wl->count++;
    return 0;
}

int WhitelistContains(Whitelist* wl, const char* domain) {
    if (!wl || !domain) return 0;
    for (size_t i = 0; i < wl->count; i++) {
        if (!wl->entries[i].is_exception && match_domain(domain, wl->entries[i].domain)) {
            return 1;
        }
    }
    return 0;
}

int WhitelistContainsException(Whitelist* wl, const char* domain) {
    if (!wl || !domain) return 0;
    for (size_t i = 0; i < wl->count; i++) {
        if (wl->entries[i].is_exception && match_domain(domain, wl->entries[i].domain)) {
            return 1;
        }
    }
    return 0;
}

void WhitelistClear(Whitelist* wl) {
    if (!wl) return;
    wl->count = 0;
    memset(wl->entries, 0, sizeof(wl->entries));
}

int WhitelistLoadFromData(Whitelist* wl, const char* data, size_t size) {
    if (!wl || !data || size == 0) return -1;

    WhitelistClear(wl);

    char* copy = malloc(size + 1);
    if (!copy) return -1;
    memcpy(copy, data, size);
    copy[size] = '\0';

    // Split on newlines without strtok (strtok uses static state and races when
    // multiple IPC handlers run concurrently).
    char* line = copy;
    while (line && wl->count < MAX_WHITELIST_DOMAINS) {
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
            WhitelistAdd(wl, domain, is_exception);
        }

        line = next ? next + 1 : NULL;
    }

    free(copy);
    return 0;
}

void IpAllowlistClear(IpAllowlist* al) {
    if (!al) return;
    al->count = 0;
    memset(al->ips, 0, sizeof(al->ips));
}

void IpAllowlistInit(IpAllowlist* al) {
    memset(al, 0, sizeof(IpAllowlist));
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

    if (al->count >= MAX_ALLOWED_IPS) {
        // Force cleanup and retry once before giving up
        al->last_cleared_at = 0;
        IpAllowlistCleanup(al);
        if (al->count >= MAX_ALLOWED_IPS) {
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
