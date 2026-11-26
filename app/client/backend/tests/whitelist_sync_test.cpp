#define BB_TOKEN_TEST
#include "../src/ipc_daemon.cpp"
#include <assert.h>
#include <algorithm>

HANDLE g_ServiceStopEvent = INVALID_HANDLE_VALUE;
static HANDLE ready;
static const char* server_rules = "example.com\n!blocked.com";
static int fetches;
static int fail_fetch;
static const char* state_response = "1";
static bool state_error = false;
static int legacy_requests = 0;

static int token_request(const char* command, const char** args, size_t count, char* response, size_t capacity) {
    assert(count == 2 && strcmp(args[0], "alice") == 0 && strcmp(args[1], "token") == 0);

    if (strcmp(command, "GET_PROCESS_BLACKLIST_STATE") == 0) {
        snprintf(response, capacity, "%s", state_response);
        return state_error ? -1 : 0;
    }

    if (strcmp(command, "GET_PROCESS_BLACKLIST") == 0) {
        ++legacy_requests;
        snprintf(response, capacity, "%s", server_rules);
        return 0;
    }

    assert(strcmp(command, "GET_WHITELIST") == 0);
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
    {"SET_WHITELIST\n", "OK\n"},
    {"SET_WHITELIST\n", "OK\n"},
    {"GET_IPV6_BLOCK\n", "1\n"},
    {"GET_FILTRATION\n", "1\n"},
    {"SET_WHITELIST\n", "ERROR\nunavailable\n"},
    {"GET_IPV6_BLOCK\n", "1\n"},
    {"GET_FILTRATION\n", "1\n"},
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
        char request[256] = {};
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
    SrvLineReader reader = {};
    if (data)
        reader.len = (size_t)snprintf(reader.buf, sizeof(reader.buf), "%zu\n%s\n\n", strlen(data), data);
    else
        reader.len = (size_t)snprintf(reader.buf, sizeof(reader.buf), "\n");
    int result = handle_server_event("WHITELIST_SYNC_REQUESTED", &reader, "alice");
    assert(reader.pos == reader.len);
    return result;
}

static bool parse_reply(std::string_view wire, char (&out)[16]) {
    auto read = [&wire](char* data, size_t size) {
        size_t count = std::min({wire.size(), size, size_t{2}});
        memcpy(data, wire.data(), count);
        wire.remove_prefix(count);
        return static_cast<int>(count);
    };

    return read_server_fields(read, out, sizeof(out));
}

static void test_blacklist_sync() {
    auto& monitors = client::process_monitor_control();
    assert(monitors.set_enabled(true));
    auto revision = monitors.revision();

    state_response = "0\nтест.exe\n!editor.exe";
    assert(sync_process_blacklist("alice", revision));
    auto state = monitors.state();
    assert(state.server_enabled == false && state.rules == "тест.exe\n!editor.exe");

    state_response = "1";
    assert(sync_process_blacklist("alice", revision));
    assert(monitors.state().server_enabled == true && monitors.state().rules.empty());

    assert(monitors.set_enabled(false));
    revision = monitors.revision();
    state_response = "1\nfirst.exe";
    assert(sync_process_blacklist("alice", revision));
    assert(!monitors.state().local_enabled && monitors.state().rules == "first.exe");

    state_response = "bad";
    assert(!sync_process_blacklist("alice", revision));
    state_error = true;
    assert(!sync_process_blacklist("alice", revision));
    assert(legacy_requests == 0 && monitors.state().rules == "first.exe");

    state_response = "unknown command: GET_PROCESS_BLACKLIST_STATE";
    server_rules = "legacy.exe";
    assert(sync_process_blacklist("alice", revision));
    assert(legacy_requests == 1 && monitors.state().server_enabled == true &&
           monitors.state().rules == "legacy.exe");

    server_rules = "";
    assert(sync_process_blacklist("alice", revision));
    assert(legacy_requests == 2 && !monitors.state().server_enabled.has_value() &&
           monitors.state().rules.empty());

    server_rules = "example.com\n!blocked.com";
    monitors.disconnect();
}

int main(void) {
    char response[16];

    assert(parse_reply("3\nfoo\n3\nbar\n\n", response) && strcmp(response, "foo\nbar") == 0);
    assert(parse_reply("\n", response) && !*response);

    for (auto bad : {"", "8\ngame.exe\n", "8\ngame", "8\ngame.exe!\n", "16\n0123456789abcdef\n\n", "-1\n",
                     "123456789012345678901234567890\n"}) {
        assert(!parse_reply(bad, response));
    }

    CloseHandle(CreateThread(NULL, 0, watchdog, NULL, 0, NULL));
    assert(ini_set_string("notifications", "enabled", "0") == 0);
    set_client_token("token");
    test_blacklist_sync();
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
    assert(ini_set_string("filtration", "auto_disable", "0") == 0);
    assert(ini_set_string("filtration", "block_ipv6", "1") == 0);
    SetFallbackWhitelistEnabled(0);
    assert(!IsServerSessionActive());
    block_all_if_no_fallback();
    assert(g_blocked_all_pushed);
    ResetFirewallSync();
    assert(g_blocked_all_pushed && g_whitelist_sync_pending);
    block_all_if_no_fallback();
    ResetFirewallSync();
    assert(!g_blocked_all_pushed);
    block_all_if_no_fallback();
    assert(g_blocked_all_pushed);
    assert(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    CloseHandle(thread);
    CloseHandle(ready);
    puts("PASS: whitelist synchronization, offline blocking after firewall restart and failed reset retry");
    return 0;
}
