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
    char input[128];
    snprintf(input, sizeof(input), "%s", command);
    assert(parse_and_execute(writer, input, strlen(input)) == 0);
    char response[128] = {0};
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
    puts("IPv6 blocking IPC tests passed");
    return 0;
}
