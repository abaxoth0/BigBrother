#include "../src/ipc_daemon.c"
#include <assert.h>

HANDLE g_ServiceStopEvent = INVALID_HANDLE_VALUE;
static const char* fingerprint = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
static const char* token = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
static void fixture(const char* content) {
    char dir[MAX_PATH], path[MAX_PATH];
    get_exe_path(dir, sizeof(dir));
    strncat(dir, "\\config", sizeof(dir) - strlen(dir) - 1);
    CreateDirectory(dir, NULL);
    build_ini_path(path, sizeof(path));
    FILE* file = fopen(path, "w");
    assert(file);
    assert(fputs(content, file) >= 0);
    assert(fclose(file) == 0);
}
static void value(const char* section, const char* key, const char* expected) {
    char out[128] = {0};
    assert(ini_get_string(section, key, out, sizeof(out)));
    assert(strcmp(out, expected) == 0);
}
int main(void) {
    fixture("[server]\naddress=127.0.0.1\nname=Dev\nport=1984\n\n[filtration]\nauto_disable=1\n\n[client]\nusername=dev\n\n[discovery]\nenabled=1\n\n[network]\nauto=1\n");
    assert(ini_set_string("server", "fingerprint", fingerprint) == 0);
    assert(ini_set_string("client", "token", token) == 0);
    value("server", "fingerprint", fingerprint);
    value("client", "token", token);
    char out[128];
    assert(!ini_get_string("network", "fingerprint", out, sizeof(out)));
    assert(!ini_get_string("network", "token", out, sizeof(out)));
    value("network", "auto", "1");

    char broken[1024];
    snprintf(broken, sizeof(broken), "[server]\naddress=127.0.0.1\n[client]\nusername=dev\n[network]\nauto=1\nfingerprint=%s\ntoken=%s\n", fingerprint, token);
    fixture(broken);
    value("server", "fingerprint", fingerprint); // Reading repairs legacy placement.
    value("client", "token", token);
    assert(!ini_get_string("network", "fingerprint", out, sizeof(out)));
    assert(!ini_get_string("network", "token", out, sizeof(out)));
    value("network", "auto", "1");

    fixture("[discovery]\nenabled=1\n[network]\nenabled=0\n");
    assert(ini_set_string("discovery", "enabled", "2") == 0);
    value("discovery", "enabled", "2");
    value("network", "enabled", "0");
    fixture("[server]\n[network]\nauto=1");
    assert(ini_set_string("server", "fingerprint", fingerprint) == 0);
    value("server", "fingerprint", fingerprint);
    value("network", "auto", "1");
    fixture("[server]");
    assert(ini_set_string("server", "fingerprint", fingerprint) == 0);
    value("server", "fingerprint", fingerprint);

    snprintf(broken, sizeof(broken), "[server]\nfingerprint=\n[client]\ntoken=\n[network]\nfingerprint=%s\ntoken=%s\n", fingerprint, token);
    fixture(broken);
    value("server", "fingerprint", ""); // An explicit clear must not be undone.
    value("client", "token", "");

    snprintf(broken, sizeof(broken), "[server]\n[network]\nfingerprint=%s\n[other]\nfingerprint=CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC\n", fingerprint);
    fixture(broken);
    value("server", "fingerprint", "AMBIGUOUS_MISPLACED_CREDENTIAL");
    fixture("[server]\n[network]\nfingerprint=bad-pin\n");
    value("server", "fingerprint", "INVALID_MISPLACED_CREDENTIAL");
    puts("INI section and misplaced-credential repair tests passed");
    return 0;
}
