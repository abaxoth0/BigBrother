#define BB_TOKEN_TEST
#include "../src/ipc_daemon.c"
#include <assert.h>

HANDLE g_ServiceStopEvent = INVALID_HANDLE_VALUE;
static HANDLE ready;
static const char* expected_revision = "a0f458ff7ae9f5978d2beb0a07253af0bd3ccd28dbe30e11adc08fc86f6c05e9";
static int session_requests;

static int token_request(const char* command, const char** args, size_t count, char* response, size_t capacity) {
    (void)args; (void)count;
    if (strcmp(command, "CONNECT") == 0 || strcmp(command, "DISCONNECT") == 0) {
        session_requests++;
        snprintf(response, capacity, "OK");
        return 0;
    }
    assert(strcmp(command, "GET_WHITELIST") == 0);
    snprintf(response, capacity, "example.com\n!blocked.com");
    return 0;
}

static const struct { const char* command; const char* response; } requests[] = {
    {"SET_WHITELIST\nexample.com\n!blocked.com", "ERROR\nfailed\n"},
    {"SET_WHITELIST\nexample.com\n!blocked.com", "OK\n"},
    {"GET_IPV6_BLOCK\n", "0\n"},
    {"GET_FILTRATION\n", "1\n"},
    {"GET_WHITELIST\n", "WHITELIST\nEXAMPLE.com\n!blocked.com\n"},
    {"GET_IPV6_BLOCK\n", "0\n"},
    {"SET_IPV6_BLOCK\n1", "OK\n"},
    {"GET_IPV6_BLOCK\n", "1\n"},
    {"SET_IPV6_BLOCK\n0", "OK\n"},
    {"SET_FILTRATION\n0", "OK\n"},
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
        assert(strcmp(request, requests[i].command) == 0);
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
    fputs("FAIL: policy test timed out\n", stderr);
    ExitProcess(1);
    return 0;
}

static int ipv6_event(const char* name, const char* enabled) {
    SrvLineReader reader = {0};
    reader.len = (size_t)snprintf(reader.buf, sizeof(reader.buf), "%zu\n%s\n%zu\n%s\n\n",
        strlen(name), name, strlen(enabled), enabled);
    int result = handle_server_event("CLIENT_IPV6_BLOCK_CHANGED", &reader, "alice");
    assert(reader.pos == reader.len);
    return result;
}

static int disconnect_event(const char* name) {
    SrvLineReader reader = {0};
    reader.len = (size_t)snprintf(reader.buf, sizeof(reader.buf), "%zu\n%s\n\n", strlen(name), name);
    int result = handle_server_event("CLIENT_DISCONNECTED", &reader, "alice");
    assert(reader.pos == reader.len);
    return result;
}

int main(void) {
    char rules[] = " EXAMPLE.com\r\n!blocked.com\nexample.com\n# ignored\n; ignored\n";
    char revision[65];
    assert(whitelist_revision(rules, revision) == 0);
    assert(strcmp(revision, expected_revision) == 0);
    char empty[] = "";
    assert(whitelist_revision(empty, revision) == 0);
    assert(strcmp(revision, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
    assert(ini_set_string("notifications", "enabled", "0") == 0);
    assert(ini_set_string("filtration", "block_ipv6", "0") == 0);
    assert(ini_set_string("filtration", "auto_disable", "0") == 0);
    set_client_token("token");
    CloseHandle(CreateThread(NULL, 0, watchdog, NULL, 0, NULL));
    ready = CreateEvent(NULL, TRUE, FALSE, NULL);
    HANDLE thread = CreateThread(NULL, 0, firewall, NULL, 0, NULL);
    assert(thread && WaitForSingleObject(ready, 5000) == WAIT_OBJECT_0);
    sync_whitelist_from_server("alice");
    assert(g_whitelist_sync_pending && !wl_last_equals("example.com\n!blocked.com"));
    sync_whitelist_from_server("alice");
    assert(!g_whitelist_sync_pending && wl_last_equals("example.com\n!blocked.com"));
    char message[256], expected[256];
    build_policy_heartbeat(message, sizeof(message));
    snprintf(expected, sizeof(expected),
        "POLICY {\"whitelistRevision\":\"%s\",\"filtrationEnabled\":true,\"blockIpv6\":false,\"canSetIpv6Block\":true}\n", expected_revision);
    assert(strcmp(message, expected) == 0);
    assert(ipv6_event("name=alicex", "enabled=1") == 0);
    assert(ipv6_event("n=alice", "enabled=1") == 0);
    assert(ipv6_event("name=alice", "enabled=true") == 0);
    assert(ipv6_event("name=alice", "enabled=01") == 0);
    assert(IsIpv6BlockEnabled() == 0);
    assert(ipv6_event("name=alice", "enabled=1") == 2);
    assert(IsIpv6BlockEnabled() == 1);
    assert(ipv6_event("name=alice", "enabled=0") == 2);
    assert(IsIpv6BlockEnabled() == 0);
    SetServerSessionActive(1);
    assert(ini_set_string("filtration", "auto_disable", "1") == 0);
    assert(disconnect_event("name=alicex") == 0);
    assert(IsServerSessionActive() && !is_server_connection_paused());
    assert(disconnect_event("name=alice") == 1);
    assert(!IsServerSessionActive() && is_server_connection_paused());
    assert(!g_filtration_enabled && g_filtration_requested);
    SetServerSessionActive(1);
    assert(!IsServerSessionActive());
    assert(ServerConnect("alice") == -1 && ServerRefresh("alice") == -1);
    assert(session_requests == 0);
    int failures = 0;
    assert(server_subscribe_loop("alice", &failures, 1) == 0);
    assert(ini_set_string("filtration", "auto_disable", "0") == 0);
    assert(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    CloseHandle(thread);
    CloseHandle(ready);
    char config[MAX_PATH];
    build_ini_path(config, sizeof(config));
    HANDLE locked = CreateFileA(config, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    assert(locked != INVALID_HANDLE_VALUE);
    assert(ipv6_event("name=alice", "enabled=1") == 2);
    assert(IsIpv6BlockEnabled() == 0);
    CloseHandle(locked);
    assert(ipv6_event("name=alice", "enabled=1") == 2);
    assert(IsIpv6BlockEnabled() == 1);
    build_policy_heartbeat(message, sizeof(message));
    assert(strcmp(message, "POLICY {\"whitelistRevision\":\"\",\"filtrationEnabled\":false,\"blockIpv6\":true,\"canSetIpv6Block\":true}\n") == 0);
    assert(ipv6_event("name=alice", "enabled=0") == 2);
    build_policy_heartbeat(message, sizeof(message));
    assert(strcmp(message, "POLICY {\"whitelistRevision\":\"\",\"filtrationEnabled\":false,\"blockIpv6\":false,\"canSetIpv6Block\":true}\n") == 0);
    g_filtration_requested = 0;
    LONG revision_before = g_server_connection_revision;
    ResumeServerConnection();
    assert(!is_server_connection_paused() && g_server_connection_revision != revision_before);
    assert(ServerConnect("alice") == 0 && IsServerSessionActive());
    assert(ServerDisconnect("alice") == 0 && !IsServerSessionActive() && is_server_connection_paused());
    assert(session_requests == 2);
    puts("PASS: policy reports, whitelist retry, targeted IPv6 control, persistence, disconnect pause and manual reconnect");
    return 0;
}
