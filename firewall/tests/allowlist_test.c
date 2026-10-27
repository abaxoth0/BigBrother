#include "../include/allowlist.h"
#include "../include/dns.h"
#include <assert.h>
#include <stdio.h>

static void policy(Whitelist* wl, Whitelist* bl, const char* text) {
    assert(WhitelistLoadFromData(wl, bl, text, strlen(text)) == 0);
}

static void ports_test(void) {
    Whitelist wl = {0}, bl = {0};
    IpAllowlist ips = {0}, blocked = {0};
    uint32_t ip = 0x01010101;
    time_t now = time(NULL);
    const char* rules[] = {
        "1.1.1.1", "1.1.1.1:443", "1.1.1.1:{0-1023}",
        "1.1.1.1:443|80|53|20|22", "1.1.1.1:{0-1023|5432|2000-3000}"
    };
    for (size_t r = 0; r < sizeof(rules) / sizeof(rules[0]); r++) {
        policy(&wl, &bl, rules[r]);
        assert(strcmp(wl.entries[0].domain, "1.1.1.1") == 0);
        assert(strcmp(wl.entries[0].text, rules[r]) == 0);
        for (int port = 0; port <= 65535; port++) {
            int expected = r == 0 || (r == 1 && port == 443) || (r == 2 && port <= 1023) ||
                (r == 3 && (port == 443 || port == 80 || port == 53 || port == 20 || port == 22)) ||
                (r == 4 && (port <= 1023 || port == 5432 || (port >= 2000 && port <= 3000)));
            assert(WhitelistAllows(&wl, &bl, &ips, &blocked, ip, port, now) == expected);
        }
        assert(WhitelistAllows(&wl, &bl, &ips, &blocked, ip, RULE_NO_PORT, now) == (r == 0));
        assert(!WhitelistAllows(&wl, &bl, &ips, &blocked, 0x02020202, 443, now));
    }
    policy(&wl, &bl, "1.1.1.1:0|65535");
    assert(WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 65535, now));
    assert(WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 0, now));
    const char* invalid[] = {"", "{}", "{1", "1}", "{1}}", "{1|}", "|1", "1||2", "1|", "-1", "1-", "2-1", "65536", "1-65536", "9999999999999", "1:2", "+1", "1 2", "{1-2-3}"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        char input[128];
        snprintf(input, sizeof(input), "other.example\n1.1.1.1:%s", invalid[i]);
        assert(WhitelistLoadFromData(&wl, &bl, input, strlen(input)) == -1);
        assert(wl.count == 1 && strcmp(wl.entries[0].text, "1.1.1.1:0|65535") == 0);
    }

    policy(&wl, &bl, "*.example.com:{80|443}\n\"database\":5432\n!blocked.example.com:443");
    assert(IpAllowlistAdd(&ips, ip, "www.example.com", 300) == 0);
    assert(IpAllowlistAdd(&ips, ip, "database.test", 300) == 0);
    assert(IpAllowlistAdd(&blocked, ip, "blocked.example.com", 300) == 0);
    assert(ips.count == 1);
    assert(WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 80, now));
    assert(WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 5432, now));
    assert(!WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 443, now));
    assert(!WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 22, now));
    assert(IpAllowlistAdd(&ips, ip, "blocked.example.com", 300) == 0);
    IpAllowlistClear(&blocked);
    assert(!WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 443, now));
    assert(WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 80, now));

    policy(&wl, &bl, "*.example.com:22\n!1.1.1.1:443");
    IpAllowlistPurgeUnowned(&ips, &wl, NULL);
    assert(!WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 80, now));
    assert(!WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 5432, now));
    assert(WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 22, now));
    policy(&wl, &bl, "1.1.1.1\n!1.1.1.1:{443|2000-3000}");
    assert(!WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 443, now));
    assert(!WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 2500, now));
    assert(WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 80, now));
    assert(WhitelistAllows(&wl, &bl, &ips, &blocked, ip, RULE_NO_PORT, now));
    policy(&wl, &bl, "1.1.1.1\n!1.1.1.1");
    assert(!WhitelistAllows(&wl, &bl, &ips, &blocked, ip, RULE_NO_PORT, now));

    policy(&wl, &bl, "*.example.com");
    AllowedIp* cached = IpAllowlistLookup(&ips, ip, now);
    assert(cached && cached->domains && cached->domains->next);
    cached->domains->expires = now - 1;
    IpAllowlistCleanup(&ips);
    assert(ips.count == 1);
    assert(WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 80, now));
    for (CachedDomain* d = cached->domains; d; d = d->next) d->expires = now - 1;
    assert(!WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 80, now));
    IpAllowlistCleanup(&ips);
    assert(ips.count == 0);
    WhitelistFree(&wl);
    WhitelistFree(&bl);
    IpAllowlistFree(&ips);
    IpAllowlistFree(&blocked);
}

static void priorities_test(void) {
    Whitelist wl = {0}, bl = {0};
    IpAllowlist ips = {0}, blocked = {0};
    uint32_t ip = 0x01010101;
    time_t now = time(NULL);
    policy(&wl, &bl, "# priority default\n[10]\n*.github.com:443\n[0]fallback.org\n[20]!gist.github.com\n[0]!\"github\"");
    assert(wl.count == 2 && bl.count == 2);
    assert(wl.entries[0].priority == 10 && strcmp(wl.entries[0].text, "[10]*.github.com:443") == 0);
    assert(wl.entries[1].priority == 0 && strcmp(wl.entries[1].text, "fallback.org") == 0);
    assert(bl.entries[0].priority == 20 && strcmp(bl.entries[0].text, "[20]!gist.github.com") == 0);
    assert(IpAllowlistAdd(&blocked, ip, "api.github.com", 300) == 0);
    assert(WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 443, now));
    assert(!WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 80, now));
    assert(IpAllowlistAdd(&blocked, ip, "gist.github.com", 300) == 0);
    assert(!WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 443, now));
    policy(&wl, &bl, "[20]*.github.com:443\n[20]!gist.github.com");
    assert(!WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 443, now));
    policy(&wl, &bl, "[21]*.github.com:443\n[20]!gist.github.com");
    assert(WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 443, now));
    policy(&wl, &bl, "[20]!1.1.1.1:443\n[21]*.github.com:443");
    assert(WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 443, now));
    policy(&wl, &bl, "[20]1.1.1.1:443\n[21]!\"github\":443");
    assert(!WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 443, now));
    policy(&wl, &bl, "[2147483647]1.1.1.1:443\n[2147483646]!1.1.1.1:443");
    assert(WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 443, now));
    const char* invalid[] = {"[]example.com", "[-1]example.com", "[+1]example.com", "[2147483648]example.com",
        "[99999999999999999999999]example.com", "[default 10]", "[10example.com", "[1][2]example.com", "![10]example.com",
        "example.com\n[10]", "[10]\n[20]"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        assert(WhitelistLoadFromData(&wl, &bl, invalid[i], strlen(invalid[i])) == -1);
        assert(wl.entries[0].priority == INT32_MAX);
    }
    policy(&wl, &bl, "[10]\n[0]1.1.1.1:443\n[0]!1.1.1.1:443");
    assert(!WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 443, now));
    policy(&wl, &bl, "[10]\n# empty list");
    assert(wl.count == 0 && bl.count == 0);
    assert(!WhitelistAllows(&wl, &bl, &ips, &blocked, ip, 443, now));
    WhitelistFree(&wl);
    WhitelistFree(&bl);
    IpAllowlistFree(&ips);
    IpAllowlistFree(&blocked);
}

static int64_t reference_priority(const Whitelist* rules, const IpAllowlist* learned, const IpAllowlist* blocked,
                                  uint32_t ip, int port, time_t now) {
    int64_t priority = -1;
    for (size_t i = 0; i < rules->count; i++) {
        const WhitelistEntry* rule = &rules->entries[i];
        int port_match = rule->port_count == 0;
        for (size_t j = 0; j < rule->port_count; j++)
            if (port >= rule->ports[j].first && port <= rule->ports[j].last) port_match = 1;
        if (!port_match) continue;
        int matches = rule->is_ip && rule->ip == ip;
        const IpAllowlist* caches[] = {learned, blocked};
        for (size_t c = 0; c < 2; c++) {
            AllowedIp* entry = NULL;
            HASH_FIND_INT(caches[c]->head, &ip, entry);
            for (const CachedDomain* d = entry ? entry->domains : NULL; d; d = d->next)
                if (d->expires > now && DnsCheckDomainLower(d->domain, rule->pattern_lower)) matches = 1;
        }
        if (matches && rule->priority > priority) priority = rule->priority;
    }
    return priority;
}

static void sorted_matching_test(void) {
    Whitelist wl = {0}, bl = {0};
    IpAllowlist learned = {0}, blocked = {0};
    assert(IpAllowlistAdd(&learned, 0x01010101, "api.example.com", 300) == 0);
    assert(IpAllowlistAdd(&learned, 0x01010101, "database.test", 300) == 0);
    assert(IpAllowlistAdd(&blocked, 0x02020202, "blocked.example.com", 300) == 0);
    assert(IpAllowlistAdd(&blocked, 0x02020202, "max.example", 300) == 0);
    assert(IpAllowlistAdd(&learned, 0x03030303, "api.example.com", 300) == 0);
    assert(IpAllowlistAdd(&blocked, 0x03030303, "max.example", 300) == 0);
    time_t now = time(NULL);
    AllowedIp* expired = IpAllowlistLookup(&blocked, 0x03030303, now);
    assert(expired);
    expired->domains->expires = now - 1;
    const char* rules[] = {
        "[2]api.example.com:80|443", "[10]!*.example.com:443", "[20]*.example.com:443",
        "[20]!blocked.example.com:443", "[1]1.1.1.1", "[7]!1.1.1.1:80", "[9]\"database\":5432",
        "[100]unrelated.example", "[2147483647]max.example:22", "[2147483646]!max.example:22",
        "[0]!\"example\"", "[30]2.2.2.2:22", "[30]!2.2.2.2:22", "[20]3.3.3.3:443",
        "[20]!3.3.3.3:443", "[40]1.1.1.1:0", "[40]!1.1.1.1:0", "[5]*.example.com"
    };
    const int ports[] = {RULE_NO_PORT, 0, 22, 53, 80, 443, 5432, 65535};
    for (size_t permutation = 0; permutation < 36; permutation++) {
        char text[4096] = "";
        size_t count = sizeof(rules) / sizeof(rules[0]);
        for (size_t i = 0; i < count; i++) {
            size_t index = permutation < count ? (i + permutation) % count :
                (count - 1 - i + permutation) % count;
            strcat(text, rules[index]);
            strcat(text, "\n");
        }
        policy(&wl, &bl, text);
        const Whitelist* lists[] = {&wl, &bl};
        for (size_t list = 0; list < 2; list++)
            for (size_t i = 1; i < lists[list]->count; i++)
                assert(lists[list]->entries[i - 1].priority >= lists[list]->entries[i].priority);
        for (uint32_t ip = 0x01010101; ip <= 0x04040404; ip += 0x01010101) {
            for (size_t p = 0; p < sizeof(ports) / sizeof(ports[0]); p++) {
                for (int after_expiry = 0; after_expiry < 2; after_expiry++) {
                    time_t at = now + after_expiry * 301;
                    int64_t allow = reference_priority(&wl, &learned, &blocked, ip, ports[p], at);
                    int64_t deny = reference_priority(&bl, &learned, &blocked, ip, ports[p], at);
                    assert(WhitelistAllows(&wl, &bl, &learned, &blocked, ip, ports[p], at) == (allow > deny));
                }
            }
        }
    }
    WhitelistFree(&wl);
    WhitelistFree(&bl);
    assert(WhitelistAdd(&wl, "[1]1.1.1.1:443") == 0);
    assert(WhitelistAdd(&wl, "[100]1.1.1.1:443") == 0);
    assert(WhitelistAdd(&bl, "[50]!1.1.1.1:443") == 0);
    assert(wl.entries[0].priority == 100);
    assert(WhitelistAllows(&wl, &bl, &learned, &blocked, 0x01010101, 443, now));
    assert(WhitelistAdd(&bl, "[100]!1.1.1.1:443") == 0);
    assert(!WhitelistAllows(&wl, &bl, &learned, &blocked, 0x01010101, 443, now));
    WhitelistFree(&wl);
    WhitelistFree(&bl);
    IpAllowlistFree(&learned);
    IpAllowlistFree(&blocked);
}

int main(void) {
    ports_test();
    priorities_test();
    sorted_matching_test();
    Whitelist wl = {0}, bl = {0};
    IpAllowlist ips = {0};
    const char initial[] = "8.8.8.8\nexample.com\n!blocked.example.com\n";
    assert(WhitelistLoadFromData(&wl, &bl, initial, sizeof(initial) - 1) == 0);
    assert(wl.count == 2 && bl.count == 1);
    assert(IpAllowlistAdd(&ips, 0x08080808, "8.8.8.8", 300) == 0);
    assert(IpAllowlistAdd(&ips, 0x01010101, "example.com", 300) == 0);

    // Keeping a literal IP rule preserves its cache entry.
    IpAllowlistPurgeUnowned(&ips, &wl, NULL);
    assert(ips.count == 2);

    // A nonempty replacement must revoke a removed literal IP immediately.
    const char replacement[] = "example.com\n";
    assert(WhitelistLoadFromData(&wl, &bl, replacement, sizeof(replacement) - 1) == 0);
    IpAllowlistPurgeUnowned(&ips, &wl, NULL);
    assert(!IpAllowlistContains(&ips, 0x08080808));
    assert(IpAllowlistContains(&ips, 0x01010101));
    assert(bl.count == 0);

    // Rejected input leaves the installed policy intact.
    assert(WhitelistLoadFromData(&wl, &bl, NULL, 1) == -1);
    assert(wl.count == 1);

    // Repeated cleanup/reload must release the previous hash table.
    IpAllowlistClear(&ips);
    assert(ips.head == NULL && ips.count == 0);
    assert(IpAllowlistAdd(&ips, 0x08080808, "8.8.8.8", 300) == 0);
    IpAllowlistClear(&ips);
    WhitelistFree(&wl);
    WhitelistFree(&bl);
    puts("allowlist regression tests passed");
    return 0;
}
