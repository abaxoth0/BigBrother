#include <assert.h>
#include <stdio.h>

// Exercise the firewall request dispatcher without loading the WinDivert driver.
#include "../src/ipc.c"

Whitelist g_Whitelist = {0};
Whitelist g_Blacklist = {0};
IpAllowlist g_IpAllowlist = {0};
IpAllowlist g_IpBlocklist = {0};
HANDLE g_ServiceStopEvent = NULL;
SRWLOCK g_AllowlistLock = SRWLOCK_INIT;
int g_FiltrationEnabled = 1;
volatile LONG g_BlockIpv6 = 1;

int LoadWhiteList(char* path) { (void)path; return 0; }
void PreResolveWhitelist(void) {}

static void request(const char* command, const char* expected) {
    HANDLE reader, writer;
    assert(CreatePipe(&reader, &writer, NULL, 0));
    char input[4096];
    snprintf(input, sizeof(input), "%s", command);
    assert(parse_and_execute(writer, input, strlen(input)) == 0);
    char response[4096] = {0};
    DWORD received;
    assert(ReadFile(reader, response, sizeof(response) - 1, &received, NULL));
    assert(strcmp(response, expected) == 0);
    CloseHandle(reader);
    CloseHandle(writer);
}

int main(void) {
    request("GET_IPV6_BLOCK\n", "1\n");
    request("SET_IPV6_BLOCK\n0", "OK\n");
    assert(g_BlockIpv6 == 0);
    assert(g_FiltrationEnabled == 1);
    request("GET_IPV6_BLOCK\n", "0\n");
    request("SET_IPV6_BLOCK\ninvalid", "ERROR\nexpected value 0 or 1\n");
    assert(g_BlockIpv6 == 0);
    request("SET_IPV6_BLOCK\n", "ERROR\nexpected value 0 or 1\n");
    assert(g_BlockIpv6 == 0);
    request("SET_IPV6_BLOCK\n1", "OK\n");
    assert(g_BlockIpv6 == 1);
    assert(g_FiltrationEnabled == 1);
    request("SET_WHITELIST\n1.1.1.1:{0-1023|5432|2000-3000}\n!1.1.1.1:443|80\n", "OK\n");
    request("GET_WHITELIST\n", "WHITELIST\n1.1.1.1:{0-1023|5432|2000-3000}\n!1.1.1.1:443|80\n");
    request("SET_WHITELIST\n1.1.1.1:65536\n", "ERROR\nfailed to set whitelist\n");
    request("GET_WHITELIST\n", "WHITELIST\n1.1.1.1:{0-1023|5432|2000-3000}\n!1.1.1.1:443|80\n");
    char long_rule[2048] = "1.1.1.1:{";
    for (int i = 0; i < 100; i++) strcat(long_rule, "80|");
    strcat(long_rule, "65535}");
    char command[4096], expected[4096];
    snprintf(command, sizeof(command), "SET_WHITELIST\n%s\n", long_rule);
    request(command, "OK\n");
    snprintf(expected, sizeof(expected), "WHITELIST\n%s\n", long_rule);
    request("GET_WHITELIST\n", expected);
    request("SET_WHITELIST\n!\"github\"\n", "OK\n");
    assert(IpAllowlistAdd(&g_IpBlocklist, 0x01010101, "api.github.com", 300) == 0);
    request("SET_WHITELIST\n[10]\n*.github.com:443\n[20]!gist.github.com\n[0]!\"github\"\n", "OK\n");
    request("GET_WHITELIST\n", "WHITELIST\n[10]*.github.com:443\n[20]!gist.github.com\n!\"github\"\n");
    assert(g_IpBlocklist.count == 1);
    assert(WhitelistAllows(&g_Whitelist, &g_Blacklist, &g_IpAllowlist, &g_IpBlocklist, 0x01010101, 443, time(NULL)));
    request("SET_WHITELIST\nexample.com\n[10]\n", "ERROR\nfailed to set whitelist\n");
    request("GET_WHITELIST\n", "WHITELIST\n[10]*.github.com:443\n[20]!gist.github.com\n!\"github\"\n");
    request("SET_WHITELIST\n[10]*.github.com:443\n", "OK\n");
    assert(g_IpBlocklist.count == 1);
    assert(WhitelistAllows(&g_Whitelist, &g_Blacklist, &g_IpAllowlist, &g_IpBlocklist, 0x01010101, 443, time(NULL)));
    request("SET_WHITELIST\n!\"github\"\n", "OK\n");
    assert(!WhitelistAllows(&g_Whitelist, &g_Blacklist, &g_IpAllowlist, &g_IpBlocklist, 0x01010101, 443, time(NULL)));
    request("SET_WHITELIST\n", "OK\n");
    request("GET_WHITELIST\n", "WHITELIST\n");
    WhitelistFree(&g_Whitelist);
    WhitelistFree(&g_Blacklist);
    IpAllowlistFree(&g_IpAllowlist);
    IpAllowlistFree(&g_IpBlocklist);
    puts("IPv6 blocking and port-rule IPC tests passed");
    return 0;
}
