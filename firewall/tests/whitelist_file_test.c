#define main firewall_main
#include "../src/firewall.c"
#undef main

static void write_policy(const char* path, const char* rules) {
    FILE* file = fopen(path, "w");
    assert(file);
    assert(fputs(rules, file) >= 0);
    assert(fclose(file) == 0);
}

int main(void) {
    char directory[MAX_PATH], path[MAX_PATH];
    assert(GetTempPathA(sizeof(directory), directory));
    assert(GetTempFileNameA(directory, "bbw", 0, path));
    char rules[2048] = "1.1.1.1:{";
    for (int i = 0; i < 100; i++) strcat(rules, "80|");
    strcat(rules, "65535}\n!1.1.1.1:443\nEXAMPLE.COM:443\n");
    write_policy(path, rules);
    assert(LoadWhiteList(path) == STATUS_OK);
    assert(g_Whitelist.count == 2 && g_Blacklist.count == 1);
    assert(g_Whitelist.entries[0].port_count == 101);
    assert(WhitelistAllows(&g_Whitelist, &g_Blacklist, &g_IpAllowlist, &g_IpBlocklist,
                           0x01010101, 80, time(NULL)));
    assert(!WhitelistAllows(&g_Whitelist, &g_Blacklist, &g_IpAllowlist, &g_IpBlocklist,
                            0x01010101, 443, time(NULL)));
    assert(IpAllowlistAdd(&g_IpAllowlist, 0x02020202, "Example.Com", 300) == 0);
    assert(WhitelistAllows(&g_Whitelist, &g_Blacklist, &g_IpAllowlist, &g_IpBlocklist,
                           0x02020202, 443, time(NULL)));
    write_policy(path, "1.1.1.1:65536\n");
    assert(LoadWhiteList(path) == STATUS_FAILED_TO_READ_WHITELIST);
    assert(g_Whitelist.count == 2 && g_Blacklist.count == 1);
    assert(g_IpAllowlist.count == 1);
    write_policy(path, "EXAMPLE.COM:80\n");
    assert(LoadWhiteList(path) == STATUS_OK);
    assert(!WhitelistAllows(&g_Whitelist, &g_Blacklist, &g_IpAllowlist, &g_IpBlocklist,
                            0x02020202, 443, time(NULL)));
    assert(WhitelistAllows(&g_Whitelist, &g_Blacklist, &g_IpAllowlist, &g_IpBlocklist,
                           0x02020202, 80, time(NULL)));
    write_policy(path, "# default\n[10]\nEXAMPLE.COM:443\n[0]!example.com:443\n");
    assert(LoadWhiteList(path) == STATUS_OK);
    assert(g_Whitelist.count == 1 && g_Whitelist.entries[0].priority == 10);
    assert(WhitelistAllows(&g_Whitelist, &g_Blacklist, &g_IpAllowlist, &g_IpBlocklist,
                           0x02020202, 443, time(NULL)));
    write_policy(path, "[0]\nEXAMPLE.COM:443\n!example.com:443\n");
    assert(LoadWhiteList(path) == STATUS_OK);
    assert(!WhitelistAllows(&g_Whitelist, &g_Blacklist, &g_IpAllowlist, &g_IpBlocklist,
                            0x02020202, 443, time(NULL)));
    write_policy(path, "");
    assert(LoadWhiteList(path) == STATUS_OK);
    assert(!g_Whitelist.count && !g_Blacklist.count && !g_IpAllowlist.count);
    assert(DeleteFileA(path));
    WhitelistFree(&g_Whitelist);
    WhitelistFree(&g_Blacklist);
    IpAllowlistFree(&g_IpAllowlist);
    IpAllowlistFree(&g_IpBlocklist);
    puts("Whitelist file loading, long rules, case folding and port updates passed");
    return 0;
}
