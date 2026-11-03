#define BB_TOKEN_TEST
#include "../src/ipc_daemon.c"
#include <assert.h>

HANDLE g_ServiceStopEvent = INVALID_HANDLE_VALUE;
static HANDLE ready;
static const char* server_rules = "example.com\n!blocked.com";
static int fetches;
static int fail_fetch;

static int token_request(const char* command, const char** args, size_t count, char* response, size_t capacity) {
    assert(strcmp(command, "GET_WHITELIST") == 0);
    assert(count == 2 && strcmp(args[0], "alice") == 0 && strcmp(args[1], "token") == 0);
    fetches++;
    if (fail_fetch) return -1;
    snprintf(response, capacity, "%s", server_rules);
    return 0;
}

static const struct { const char* command; const char* response; } requests[] = {
    {"GET_WHITELIST\n", "WHITELIST\nEXAMPLE.com\n!blocked.com\n"},
    {"GET_WHITELIST\n", "WHITELIST\nstale.com\n"},
    {"SET_WHITELIST\nexample.com\n!blocked.com", "ERROR\nfailed\n"},
    {"SET_WHITELIST\nexample.com\n!blocked.com", "OK\n"},
    {"GET_WHITELIST\n", "WHITELIST\nstale.com\n"},
    {"SET_WHITELIST\n", "OK\n"},
    {"GET_WHITELIST\n", "WHITELIST\n"},
    {"GET_WHITELIST\n", "ERROR\nunavailable\n"},
    {"SET_WHITELIST\n", "ERROR\nunavailable\n"},
    {"SET_WHITELIST\n", "OK\n"},
};

static HANDLE new_pipe(void) {
    HANDLE pipe = CreateNamedPipeA("\\\\.\\pipe\\" DAEMON_PIPE_NAME,
        PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        2, 4096, 4096, 0, NULL);
    assert(pipe != INVALID_HANDLE_VALUE);
    return pipe;
}

static DWORD WINAPI firewall(LPVOID context) {
    (void)context;
    HANDLE pipe = new_pipe();
    SetEvent(ready);
    for (size_t i = 0; i < sizeof(requests) / sizeof(requests[0]); i++) {
        assert(ConnectNamedPipe(pipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED);
        char request[256] = {0};
        DWORD size;
        assert(ReadFile(pipe, request, sizeof(request) - 1, &size, NULL));
        if (strcmp(request, requests[i].command) != 0) {
            fprintf(stderr, "Request %zu: expected '%s', got '%s'\n", i, requests[i].command, request);
            ExitProcess(1);
        }
        HANDLE next = i + 1 < sizeof(requests) / sizeof(requests[0]) ? new_pipe() : INVALID_HANDLE_VALUE;
        assert(WriteFile(pipe, requests[i].response, (DWORD)strlen(requests[i].response), &size, NULL));
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
    fputs("FAIL: whitelist synchronization test timed out\n", stderr);
    ExitProcess(1);
    return 0;
}

static int sync_event(const char* data) {
    SrvLineReader reader = {0};
    if (data)
        reader.len = (size_t)snprintf(reader.buf, sizeof(reader.buf), "%zu\n%s\n\n", strlen(data), data);
    else
        reader.len = (size_t)snprintf(reader.buf, sizeof(reader.buf), "\n");
    int result = handle_server_event("WHITELIST_SYNC_REQUESTED", &reader, "alice");
    assert(reader.pos == reader.len);
    return result;
}

int main(void) {
    CloseHandle(CreateThread(NULL, 0, watchdog, NULL, 0, NULL));
    assert(ini_set_string("notifications", "enabled", "0") == 0);
    set_client_token("token");
    assert(sync_event(NULL) == 0);
    assert(sync_event("revision=invalid") == 0);
    assert(sync_event("revision=Z0f458ff7ae9f5978d2beb0a07253af0bd3ccd28dbe30e11adc08fc86f6c05e9") == 0);
    assert(sync_event("rev=a0f458ff7ae9f5978d2beb0a07253af0bd3ccd28dbe30e11adc08fc86f6c05e9") == 0);
    assert(fetches == 0);
    ready = CreateEvent(NULL, TRUE, FALSE, NULL);
    HANDLE thread = CreateThread(NULL, 0, firewall, NULL, 0, NULL);
    assert(thread && WaitForSingleObject(ready, 5000) == WAIT_OBJECT_0);
    const char* populated = "revision=a0f458ff7ae9f5978d2beb0a07253af0bd3ccd28dbe30e11adc08fc86f6c05e9";
    const char* empty = "revision=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    g_whitelist_sync_pending = 0;
    wl_last_set(server_rules);
    assert(sync_event(populated) == 2);
    assert(fetches == 0 && !g_whitelist_sync_pending);
    assert(sync_event(populated) == 2);
    assert(fetches == 1 && g_whitelist_sync_pending && !wl_last_equals(server_rules));
    sync_whitelist_from_server("alice");
    assert(fetches == 2 && !g_whitelist_sync_pending && wl_last_equals(server_rules));
    server_rules = "";
    wl_last_set("");
    assert(sync_event(empty) == 2);
    assert(fetches == 3 && !g_whitelist_sync_pending && wl_last_equals(""));
    assert(sync_event(empty) == 2);
    assert(fetches == 3 && !g_whitelist_sync_pending && wl_last_equals(""));
    fail_fetch = 1;
    assert(sync_event(empty) == 2);
    assert(fetches == 4 && g_whitelist_sync_pending && !wl_last_equals(""));
    fail_fetch = 0;
    sync_whitelist_from_server("alice");
    assert(fetches == 5 && g_whitelist_sync_pending && !wl_last_equals(""));
    sync_whitelist_from_server("alice");
    assert(fetches == 6 && !g_whitelist_sync_pending && wl_last_equals(""));
    assert(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    CloseHandle(thread);
    CloseHandle(ready);
    puts("PASS: matching rules skip downloads; drift bypasses cache; empty rules and failed fetch/application retry");
    return 0;
}
