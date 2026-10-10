#include "../include/allowlist.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    Whitelist wl = {0}, bl = {0};
    IpAllowlist ips = {0};
    const char initial[] = "8.8.8.8\nexample.com\n!blocked.example.com\n";
    assert(WhitelistLoadFromData(&wl, &bl, initial, sizeof(initial) - 1) == 0);
    assert(wl.count == 2 && bl.count == 1);
    assert(IpAllowlistAdd(&ips, 0x08080808, "8.8.8.8", 300) == 0);
    assert(IpAllowlistAdd(&ips, 0x01010101, "example.com", 300) == 0);

    // Keeping a literal IP rule preserves its cache entry.
    IpAllowlistPurgeUnowned(&ips, &wl);
    assert(ips.count == 2);

    // A nonempty replacement must revoke a removed literal IP immediately.
    const char replacement[] = "example.com\n";
    assert(WhitelistLoadFromData(&wl, &bl, replacement, sizeof(replacement) - 1) == 0);
    IpAllowlistPurgeUnowned(&ips, &wl);
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
