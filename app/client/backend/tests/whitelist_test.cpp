#include "../include/ipc_daemon.h"
#include "../include/ipc_client.h"
#include "process_monitors.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

HANDLE g_ServiceStopEvent = INVALID_HANDLE_VALUE;
static HANDLE ready;

static const struct {
    const char* firewall;
    const char* frontend;
} cases[] = {
    {"WHITELIST\nexample.com\n!blocked.com\n", "OK\n11\nexample.com\n12\n!blocked.com\n\n"},
    {"WHITELIST\n", "OK\n\n"},
    {"ERROR\nfailed to get whitelist\n", "ERROR\n24\ninvalid whitelist format\n\n"},
    {"WHITELIST\n", "OK\n\n"},
    {"WHITELIST\nexample.org\n", "OK\n11\nexample.org\n\n"},
};

static HANDLE new_firewall_pipe(void) {
    HANDLE pipe = CreateNamedPipeA("\\\\.\\pipe\\" DAEMON_PIPE_NAME,
        PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        2, 4096, 4096, 0, NULL);
    assert(pipe != INVALID_HANDLE_VALUE);
    return pipe;
}

static DWORD WINAPI firewall(LPVOID context) {
    (void)context;
    HANDLE pipe = new_firewall_pipe();
    SetEvent(ready);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        assert(ConnectNamedPipe(pipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED);
        char request[64] = {};
        DWORD count;
        assert(ReadFile(pipe, request, sizeof(request) - 1, &count, NULL));
        assert(strcmp(request, "GET_WHITELIST\n") == 0);
        HANDLE next = i + 1 < sizeof(cases) / sizeof(cases[0]) ? new_firewall_pipe() : INVALID_HANDLE_VALUE;
        assert(WriteFile(pipe, cases[i].firewall, (DWORD)strlen(cases[i].firewall), &count, NULL));
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        pipe = next;
    }
    return 0;
}

static DWORD WINAPI watchdog(LPVOID context) {
    (void)context;
    Sleep(15000);
    fputs("FAIL: whitelist pipe test timed out\n", stderr);
    ExitProcess(1);
    return 0;
}

static HANDLE connect_frontend(void) {
    DWORD started = GetTickCount();
    do {
        HANDLE pipe = CreateFileA("\\\\.\\pipe\\BigBrother.Client.Backend",
            GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (pipe != INVALID_HANDLE_VALUE) return pipe;
        Sleep(10);
    } while (GetTickCount() - started < 5000);
    assert(!"Backend pipe unavailable");
    return INVALID_HANDLE_VALUE;
}

static std::string frontend_request(const char* command) {
    client::Handle pipe(connect_frontend());
    DWORD count;
    assert(WriteFile(pipe.get(), command, (DWORD)strlen(command), &count, NULL));

    std::string response;
    char buffer[4096];
    while (ReadFile(pipe.get(), buffer, sizeof(buffer), &count, NULL) && count) {
        response.append(buffer, count);
    }

    return response;
}

int main(void) {
    HANDLE timeout = CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
    assert(timeout);
    CloseHandle(timeout);
    ready = CreateEvent(NULL, TRUE, FALSE, NULL);
    assert(ready);
    HANDLE thread = CreateThread(NULL, 0, firewall, NULL, 0, NULL);
    assert(thread);
    assert(WaitForSingleObject(ready, 5000) == WAIT_OBJECT_0);
    StartClientServer();

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        assert(frontend_request("GET_WHITELIST\n\n") == cases[i].frontend);
    }

    auto& monitors = client::process_monitor_control();
    assert(monitors.set_enabled(false));
    auto revision = monitors.revision();
    assert(monitors.publish(revision, false, "a.exe\n!игра.exe"));
    assert(frontend_request("GET_PROCESS_BLACKLIST\n\n") == "OK\n5\na.exe\n13\n!игра.exe\n\n");
    assert(frontend_request("GET_STATUS\n\n").ends_with("1\n0\n1\n0\n\n"));

    assert(monitors.publish(revision, true, ""));
    assert(frontend_request("GET_PROCESS_BLACKLIST\n\n") == "OK\n\n");
    assert(frontend_request("GET_STATUS\n\n").ends_with("1\n0\n1\n1\n\n"));

    monitors.disconnect();
    assert(frontend_request("GET_PROCESS_BLACKLIST\n\n").starts_with("ERROR\n"));

    assert(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    CloseHandle(thread);
    CloseHandle(ready);
    SignalClientServerShutdown();
    puts("PASS: firewall-to-frontend whitelist relay: populated, empty, failed, retry and re-enabled");
    return 0;
}
