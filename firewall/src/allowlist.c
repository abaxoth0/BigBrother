#include "../include/allowlist.h"
#include "../include/common.h"
#include "../include/dns.h"
#include <winsock2.h>
#include <ws2tcpip.h>
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

void WhitelistInit(Whitelist* wl) {
    if (!wl) return;
    WhitelistFree(wl);
    wl->capacity = WHITELIST_INITIAL_CAPACITY;
    wl->entries = calloc(wl->capacity, sizeof(WhitelistEntry));
    if (!wl->entries) {
        wl->capacity = 0;
    }
}

static int parse_port(const char** cursor, unsigned* value) {
    const char* p = *cursor;
    if (*p < '0' || *p > '9') return -1;
    unsigned n = 0;
    do {
        n = n * 10 + (*p++ - '0');
        if (n > 65535) return -1;
    } while (*p >= '0' && *p <= '9');
    *cursor = p;
    *value = n;
    return 0;
}

int WhitelistAdd(Whitelist* wl, const char* text) {
    if (!wl || !text) return -1;
    WhitelistEntry entry = {0};
    const char* colon = strchr(text, ':');
    size_t host_len = colon ? (size_t)(colon - text) : strlen(text);
    if (!host_len || host_len >= MAX_DOMAIN_LEN) return -1;
    memcpy(entry.domain, text, host_len);
    STR_COPY_LOWER(entry.pattern_lower, entry.domain, MAX_DOMAIN_LEN);
    ToLowerInplace(entry.pattern_lower);
    struct in_addr addr;
    entry.is_ip = inet_pton(AF_INET, entry.domain, &addr) == 1;
    entry.ip = entry.is_ip ? ntohl(addr.s_addr) : 0;
    if (colon) {
        const char* p = colon + 1;
        int braces = *p == '{';
        if (braces) p++;
        size_t capacity = 0;
        for (;;) {
            unsigned first, last;
            if (parse_port(&p, &first) != 0) goto invalid;
            last = first;
            if (*p == '-') {
                p++;
                if (parse_port(&p, &last) != 0 || first > last) goto invalid;
            }
            if (grow((void**)&entry.ports, &capacity, sizeof(PortRange), entry.port_count, 1, 4)) goto invalid;
            entry.ports[entry.port_count++] = (PortRange){first, last};
            if (*p != '|') break;
            p++;
        }
        if (braces && *p++ != '}') goto invalid;
        if (*p) goto invalid;
    }
    for (size_t i = 0; i < wl->count; i++) {
        if (_stricmp(wl->entries[i].text, text) == 0) {
            free(entry.ports);
            return 0;
        }
    }
    entry.text = strdup(text);
    if (!entry.text) goto invalid;
    if (grow((void**)&wl->entries, &wl->capacity, sizeof(WhitelistEntry), wl->count, 1, WHITELIST_INITIAL_CAPACITY)) {
        free(entry.text);
        goto invalid;
    }
    entry.added_time = time(NULL);
    wl->entries[wl->count++] = entry;
    return 0;
invalid:
    free(entry.ports);
    return -1;
}

void WhitelistClear(Whitelist* wl) {
    if (!wl) return;
    for (size_t i = 0; i < wl->count; i++) {
        free(wl->entries[i].text);
        free(wl->entries[i].ports);
    }
    wl->count = 0;
}

void WhitelistFree(Whitelist* wl) {
    if (!wl) return;
    WhitelistClear(wl);
    free(wl->entries);
    wl->entries = NULL;
    wl->capacity = 0;
}

static int port_matches(const WhitelistEntry* rule, int port) {
    if (!rule->port_count) return 1;
    if (port < 0) return 0;
    for (size_t i = 0; i < rule->port_count; i++) {
        if (port >= rule->ports[i].first && port <= rule->ports[i].last) return 1;
    }
    return 0;
}

int WhitelistMatches(const Whitelist* wl, const char* domain, int port) {
    for (size_t i = 0; i < wl->count; i++) {
        if ((port == RULE_ANY_PORT || port_matches(&wl->entries[i], port)) &&
            DnsCheckDomainLower(domain, wl->entries[i].pattern_lower)) return 1;
    }
    return 0;
}

static int ip_rules_match(const Whitelist* wl, const IpAllowlist* al, uint32_t ip, int port, time_t now) {
    for (size_t i = 0; i < wl->count; i++) {
        if (wl->entries[i].is_ip && wl->entries[i].ip == ip && port_matches(&wl->entries[i], port)) return 1;
    }
    AllowedIp* entry = NULL;
    HASH_FIND_INT(al->head, &ip, entry);
    if (entry) {
        for (CachedDomain* d = entry->domains; d; d = d->next) {
            if (d->expires > now && WhitelistMatches(wl, d->domain, port)) return 1;
        }
    }
    return 0;
}

int WhitelistAllows(const Whitelist* wl, const Whitelist* bl, const IpAllowlist* al,
                    const IpAllowlist* blocked, uint32_t ip, int port, time_t now) {
    if (ip_rules_match(bl, blocked, ip, port, now) || ip_rules_match(bl, al, ip, port, now)) return 0;
    return ip_rules_match(wl, al, ip, port, now);
}

int WhitelistLoadFromData(Whitelist* wl, Whitelist* bl, const char* data, size_t size) {
    if (!wl || !bl || !data || size == 0) return -1;

    Whitelist next_wl = {0}, next_bl = {0};

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

        while (*line == ' ' || *line == '\t' || *line == '\r') line++;
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' || line[len-1] == ' ' || line[len-1] == '\t')) {
            line[--len] = '\0';
        }

        if (len > 0 && line[0] != '#' && line[0] != ';') {
            int is_exception = (line[0] == '!');
            const char* domain = is_exception ? line + 1 : line;
            if (WhitelistAdd(is_exception ? &next_bl : &next_wl, domain) != 0) {
                WhitelistFree(&next_wl);
                WhitelistFree(&next_bl);
                free(copy);
                return -1;
            }
        }

        line = next ? next + 1 : NULL;
    }

    free(copy);
    WhitelistFree(wl);
    WhitelistFree(bl);
    *wl = next_wl;
    *bl = next_bl;
    return 0;
}

static void free_domains(CachedDomain* domain) {
    while (domain) {
        CachedDomain* next = domain->next;
        free(domain);
        domain = next;
    }
}

void IpAllowlistClear(IpAllowlist* al) {
    if (!al) return;
    AllowedIp* entry;
    AllowedIp* tmp;
    HASH_ITER(hh, al->head, entry, tmp) {
        HASH_DEL(al->head, entry);
        free_domains(entry->domains);
        free(entry);
    }
    al->head = NULL;
    al->count = 0;
}

void IpAllowlistFree(IpAllowlist* al) { IpAllowlistClear(al); }

void IpAllowlistInit(IpAllowlist* al) {
    if (!al) return;
    al->head = NULL;
    al->count = 0;
    al->last_sweep = 0;
}

void IpAllowlistCleanup(IpAllowlist* al) {
    if (!al) return;
    time_t now = time(NULL);
    AllowedIp* entry;
    AllowedIp* tmp;
    HASH_ITER(hh, al->head, entry, tmp) {
        CachedDomain** d = &entry->domains;
        while (*d) {
            if ((*d)->expires <= now) {
                CachedDomain* old = *d;
                *d = old->next;
                free(old);
            } else d = &(*d)->next;
        }
        if (!entry->domains) {
            HASH_DEL(al->head, entry);
            free(entry);
            al->count--;
        }
    }
    al->last_sweep = now;
}

int IpAllowlistAdd(IpAllowlist* al, uint32_t ip, const char* domain, uint32_t ttl) {
    if (!al || !domain || strlen(domain) >= MAX_DOMAIN_LEN) return -1;
    if (ttl < 300) ttl = 300;
    time_t now = time(NULL);
    if (now - al->last_sweep >= IP_ALLOWLIST_SWEEP_INTERVAL) IpAllowlistCleanup(al);
    AllowedIp* entry = NULL;
    HASH_FIND_INT(al->head, &ip, entry);
    CachedDomain* d = entry ? entry->domains : NULL;
    for (; d; d = d->next) {
        if (_stricmp(d->domain, domain) == 0) {
            d->expires = now + ttl;
            return 0;
        }
    }
    d = calloc(1, sizeof(*d));
    if (!d) return -1;
    STR_COPY_LOWER(d->domain, domain, MAX_DOMAIN_LEN);
    ToLowerInplace(d->domain);
    d->expires = now + ttl;
    if (!entry) {
        entry = calloc(1, sizeof(*entry));
        if (!entry) { free(d); return -1; }
        entry->ip = ip;
        HASH_ADD_INT(al->head, ip, entry);
        al->count++;
    }
    d->next = entry->domains;
    entry->domains = d;
    return 0;
}

int IpAllowlistContains(IpAllowlist* al, uint32_t ip) {
    return IpAllowlistLookup(al, ip, time(NULL)) != NULL;
}

const char* IpAllowlistGetDomain(IpAllowlist* al, uint32_t ip) {
    AllowedIp* entry = IpAllowlistLookup(al, ip, time(NULL));
    if (entry) {
        for (CachedDomain* d = entry->domains; d; d = d->next)
            if (d->expires > time(NULL)) return d->domain;
    }
    return NULL;
}

AllowedIp* IpAllowlistLookup(IpAllowlist* al, uint32_t ip, time_t now) {
    if (!al) return NULL;
    AllowedIp* entry = NULL;
    HASH_FIND_INT(al->head, &ip, entry);
    if (entry) {
        for (CachedDomain* d = entry->domains; d; d = d->next)
            if (d->expires > now) return entry;
    }
    return NULL;
}

int IpAllowlistRemove(IpAllowlist* al, uint32_t ip) {
    if (!al) return 0;
    AllowedIp* entry = NULL;
    HASH_FIND_INT(al->head, &ip, entry);
    if (!entry) return 0;
    HASH_DEL(al->head, entry);
    free_domains(entry->domains);
    free(entry);
    al->count--;
    return 1;
}

void IpAllowlistPurgeUnowned(IpAllowlist* al, const Whitelist* wl) {
    if (!al || !wl) return;
    AllowedIp* entry;
    AllowedIp* tmp;
    HASH_ITER(hh, al->head, entry, tmp) {
        CachedDomain** d = &entry->domains;
        while (*d) {
            if (!WhitelistMatches(wl, (*d)->domain, RULE_ANY_PORT)) {
                CachedDomain* old = *d;
                *d = old->next;
                free(old);
            } else d = &(*d)->next;
        }
        if (!entry->domains) {
            HASH_DEL(al->head, entry);
            free(entry);
            al->count--;
        }
    }
}
