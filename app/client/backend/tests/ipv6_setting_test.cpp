#include "../include/ipc_daemon.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

HANDLE g_ServiceStopEvent = INVALID_HANDLE_VALUE;

// Run --write then --read in separate processes to verify persistence while
// the firewall is stopped. The executable uses its isolated build directory.
int main(int argc, char** argv) {
    assert(argc == 2);
    if (strcmp(argv[1], "--write") == 0) {
        assert(ini_set_string("filtration", "block_ipv6", "") == 0);
        assert(IsIpv6BlockEnabled() == 1);
        assert(SetIpv6BlockEnabled(0) == 0);
        assert(IsIpv6BlockEnabled() == 0);
    } else {
        assert(strcmp(argv[1], "--read") == 0);
        assert(IsIpv6BlockEnabled() == 0);
        assert(SetIpv6BlockEnabled(1) == 0);
        assert(IsIpv6BlockEnabled() == 1);
    }
    puts("IPv6 preference persistence tests passed");
    return 0;
}
