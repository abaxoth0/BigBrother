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
    to_lower_inplace(domain_lower);

    strncpy(pattern_lower, pattern, MAX_DOMAIN_LEN - 1);
    pattern_lower[MAX_DOMAIN_LEN - 1] = '\0';
    to_lower_inplace(pattern_lower);

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

void Whitelist_Init(Whitelist* wl) {
    memset(wl, 0, sizeof(Whitelist));
}

int Whitelist_Add(Whitelist* wl, const char* domain) {
    if (!wl || !domain || wl->count >= MAX_WHITELIST_DOMAINS) {
        return -1;
    }
    for (size_t i = 0; i < wl->count; i++) {
        if (strcmp(wl->entries[i].domain, domain) == 0) {
            return 0;
        }
    }
    strncpy(wl->entries[wl->count].domain, domain, MAX_DOMAIN_LEN - 1);
    wl->entries[wl->count].domain[MAX_DOMAIN_LEN - 1] = '\0';
    wl->entries[wl->count].added_time = time(NULL);
    wl->count++;
    return 0;
}

int Whitelist_Contains(Whitelist* wl, const char* domain) {
    if (!wl || !domain) return 0;
    for (size_t i = 0; i < wl->count; i++) {
        if (match_domain(domain, wl->entries[i].domain)) {
            return 1;
        }
    }
    return 0;
}

void IpAllowlist_Init(IpAllowlist* al) {
    memset(al, 0, sizeof(IpAllowlist));
}

void IpAllowlist_Cleanup(IpAllowlist* al) {
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

int IpAllowlist_Add(IpAllowlist* al, uint32_t ip, const char* domain, uint32_t ttl) {
    if (!al) return -1;

    IpAllowlist_Cleanup(al);

    if (al->count >= MAX_ALLOWED_IPS) {
        return -1;
    }

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

int IpAllowlist_Contains(IpAllowlist* al, uint32_t ip) {
    if (!al) return 0;
    IpAllowlist_Cleanup(al);
    for (size_t i = 0; i < al->count; i++) {
        if (al->ips[i].ip == ip) {
            return 1;
        }
    }
    return 0;
}

const char* IpAllowlist_GetDomain(IpAllowlist* al, uint32_t ip) {
    // TODO Use hashmap for IpAllowlist.ips instead of static array
    if (!al) return NULL;
    IpAllowlist_Cleanup(al);
    for (size_t i = 0; i < al->count; i++) {
        if (al->ips[i].ip == ip) {
            return al->ips[i].domain;
        }
    }
    return NULL;
}
