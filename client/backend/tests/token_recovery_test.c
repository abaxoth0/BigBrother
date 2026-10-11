#define BB_TOKEN_TEST
#include "../src/ipc_daemon.c"
#include <assert.h>

HANDLE g_ServiceStopEvent = INVALID_HANDLE_VALUE;
static int calls, challenges, fetches;
static int transport_failure, wrong_identity, reject_replacement;

static int token_request(const char* command, const char** args, size_t count,
                         char* response, size_t capacity) {
    response[0] = '\0';
    if (strcmp(command, "GET_TOKEN_CHALLENGE") == 0) {
        challenges++;
        snprintf(response, capacity, "challenge");
        return 0;
    }
    if (strcmp(command, "GET_TOKEN") == 0) {
        fetches++;
        assert(count == 3 && strlen(args[2]) > 0);
        if (wrong_identity) return -1;
        snprintf(response, capacity, "replacement");
        return 0;
    }
    calls++;
    if (transport_failure) return -1;
    if (strcmp(args[count - 1], "replacement") == 0 && !reject_replacement) return 0;
    snprintf(response, capacity, "invalid token");
    return SERVER_INVALID_TOKEN;
}

static void reset(void) {
    calls = challenges = fetches = 0;
    transport_failure = wrong_identity = reject_replacement = 0;
    g_next_token_fetch = 0;
    set_client_token("old");
}

int main(void) {
    const char* args[] = {"alice", "original"};
    char response[256], token[256];
    reset();
    assert(send_authenticated_request("REFRESH", "alice", args, 2, 1, response, sizeof(response)) == 0);
    assert(calls == 2 && challenges == 1 && fetches == 1);
    assert(strcmp(args[1], "original") == 0);
    get_client_token(token, sizeof(token));
    assert(strcmp(token, "replacement") == 0);
    g_client_token[0] = '\0'; // Simulate memory cache loss: replacement was persisted.
    get_client_token(token, sizeof(token));
    assert(strcmp(token, "replacement") == 0);

    reset();
    transport_failure = 1;
    assert(send_authenticated_request("REFRESH", "alice", args, 2, 1, response, sizeof(response)) == -1);
    get_client_token(token, sizeof(token));
    assert(strcmp(token, "old") == 0 && challenges == 0);

    reset();
    wrong_identity = 1;
    assert(send_authenticated_request("REFRESH", "alice", args, 2, 1, response, sizeof(response)) == -1);
    get_client_token(token, sizeof(token));
    assert(token[0] == '\0' && g_client_token[0] == '\0');
    assert(calls == 1 && fetches == 1);
    assert(fetch_token_if_needed("alice") == -1 && fetches == 1); // cooldown

    reset();
    reject_replacement = 1;
    assert(send_authenticated_request("REFRESH", "alice", args, 2, 1, response, sizeof(response)) == SERVER_INVALID_TOKEN);
    assert(calls == 2 && challenges == 1 && fetches == 1);
    assert(fetch_token_if_needed("alice") == -1 && fetches == 1);

    reset();
    set_client_token("replacement");
    invalidate_client_token("old"); // A late failure must not erase a fresh token.
    get_client_token(token, sizeof(token));
    assert(strcmp(token, "replacement") == 0);
    reset();
    char path[MAX_PATH];
    build_ini_path(path, sizeof(path));
    HANDLE locked = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    assert(locked != INVALID_HANDLE_VALUE);
    invalidate_client_token("old");
    get_client_token(token, sizeof(token));
    assert(token[0] == '\0');
    set_client_token("replacement");
    get_client_token(token, sizeof(token));
    assert(strcmp(token, "replacement") == 0);
    char persisted[256];
    assert(ini_get_string("client", "token", persisted, sizeof(persisted)));
    assert(strcmp(persisted, "old") == 0); // Failed writes preserved the file.
    CloseHandle(locked);
    puts("Token recovery tests passed");
    return 0;
}
