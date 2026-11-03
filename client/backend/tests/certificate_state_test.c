#define BB_CERTIFICATE_TEST
#include "../src/ipc_daemon.c"
#include <assert.h>

HANDLE g_ServiceStopEvent = INVALID_HANDLE_VALUE;
static char remote_fingerprint[65];
static int probes;
static int change_pin_during_probe;
static int certificate_probe(const char* address, int port, const char* expected, char observed[65]) {
    assert(address[0] && port > 0);
    probes++;
    strcpy(observed, remote_fingerprint);
    if (change_pin_during_probe) ini_set_string("server", "fingerprint", remote_fingerprint);
    return !expected[0] || strcmp(expected, observed) == 0 ? 0 : -1;
}
static void pin(char output[65], char digit) { memset(output, digit, 64); output[64] = '\0'; }
static void check_pin(const char* expected) {
    char stored[65] = {0};
    ini_get_string("server", "fingerprint", stored, sizeof(stored));
    assert(strcmp(stored, expected) == 0);
}
int main(void) {
    char old[65], next[65], other[65], address[64], pinned[65], observed[65], normalized[65];
    pin(old, 'A'); pin(next, 'B'); pin(other, 'C');
    strcpy(remote_fingerprint, next);
    SetServerIp("127.0.0.1");
    assert(ini_set_string("server", "port", "1984") == 0);
    assert(ini_set_string("server", "fingerprint", old) == 0);
    assert(ini_set_string("client", "token", "keep-token") == 0);
    int port;
    assert(InspectServerCertificate(address, &port, pinned, observed) == 0);
    assert(strcmp(pinned, old) == 0 && strcmp(observed, next) == 0);
    check_pin(old); // Inspection must not change trust.
    assert(UpdateServerFingerprint(address, port, old, "invalid") == -1);
    assert(UpdateServerFingerprint(address, port, other, next) == -1);
    assert(UpdateServerFingerprint(address, port, old, old) == -1);
    check_pin(old);
    strcpy(remote_fingerprint, other);
    assert(UpdateServerFingerprint(address, port, old, next) == -1); // Peer changed.
    strcpy(remote_fingerprint, next);
    SetServerIp("127.0.0.2");
    assert(UpdateServerFingerprint(address, port, old, next) == -1);
    SetServerIp(address);
    assert(ini_set_string("server", "port", "1985") == 0);
    assert(UpdateServerFingerprint(address, port, old, next) == -1);
    assert(ini_set_string("server", "port", "1984") == 0);
    LONG revision = g_certificate_revision;
    assert(UpdateServerFingerprint(address, port, old, next) == 0);
    check_pin(next);
    assert(g_certificate_revision == revision + 1);
    char token[64];
    assert(ini_get_string("client", "token", token, sizeof(token)) && strcmp(token, "keep-token") == 0);

    assert(ini_set_string("server", "fingerprint", old) == 0);
    change_pin_during_probe = 1;
    assert(UpdateServerFingerprint(address, port, old, next) == -1);
    change_pin_during_probe = 0;
    check_pin(next); // The competing update was not overwritten.

    assert(ini_set_string("server", "fingerprint", "") == 0);
    assert(InspectServerCertificate(address, &port, pinned, observed) == 0);
    check_pin(""); // Inspection cannot create TOFU trust either.
    assert(UpdateServerFingerprint(address, port, "", next) == 0);
    check_pin(next);

    assert(ini_set_string("server", "fingerprint", old) == 0);
    char path[MAX_PATH]; build_ini_path(path, sizeof(path));
    HANDLE locked = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    assert(locked != INVALID_HANDLE_VALUE);
    revision = g_certificate_revision;
    assert(UpdateServerFingerprint(address, port, old, next) == -1);
    check_pin(old);
    assert(g_certificate_revision == revision);
    CloseHandle(locked);

    assert(bb_normalize_fingerprint(next, normalized) == 0 && strcmp(next, normalized) == 0);
    assert(bb_normalize_fingerprint("", normalized) == -1);
    assert(bb_normalize_fingerprint("1234", normalized) == -1);
    pin(other, 'G');
    assert(bb_normalize_fingerprint(other, normalized) == -1);
    puts("Certificate trust-state tests passed");
    return 0;
}
