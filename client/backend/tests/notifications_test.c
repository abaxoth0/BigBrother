#include "../include/ipc_daemon.h"
#include "notify.h"
#include <assert.h>
#include <stdio.h>

HANDLE g_ServiceStopEvent = INVALID_HANDLE_VALUE;
static HANDLE ready;
static volatile LONG filtration;
static volatile LONG delivery_error;

static void capture_error(DWORD error) {
    InterlockedExchange(&delivery_error, (LONG)error);
}

static HANDLE new_pipe(void) {
    static int first = 1;
    HANDLE pipe = CreateNamedPipeA("\\\\.\\pipe\\" DAEMON_PIPE_NAME,
        PIPE_ACCESS_DUPLEX | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0), PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        2, 4096, 4096, 0, NULL);
    assert(pipe != INVALID_HANDLE_VALUE);
    first = 0;
    return pipe;
}

static DWORD WINAPI firewall(LPVOID context) {
    (void)context;
    HANDLE pipe = new_pipe();
    SetEvent(ready);
    for (;;) {
        assert(ConnectNamedPipe(pipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED);
        char request[4096] = {0};
        DWORD count;
        assert(ReadFile(pipe, request, sizeof(request) - 1, &count, NULL));
        HANDLE next = new_pipe();
        const char* response = "OK\n";
        if (!strncmp(request, "GET_FILTRATION", 14)) response = filtration ? "1\n" : "0\n";
        else if (!strncmp(request, "SET_FILTRATION", 14)) filtration = strstr(request, "\n1") != NULL;
        else if (strstr(request, "FAIL")) response = "ERROR\nrejected\n";
        assert(WriteFile(pipe, response, (DWORD)strlen(response), &count, NULL));
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        pipe = next;
    }
}

static int notice_count(void) {
    HANDLE file = CreateFileA("notifications.log", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    char text[16384];
    DWORD count;
    assert(ReadFile(file, text, sizeof(text), &count, NULL));
    CloseHandle(file);
    int notices = 0;
    for (DWORD i = 0; i < count; i++) if (text[i] == '\n') notices++;
    return notices;
}

int main(int argc, char** argv) {
    if (argc == 5 && !strcmp(argv[1], "--send")) {
        if (!strcmp(argv[3], "4641494c5f44454c4956455259")) return ERROR_ACCESS_DENIED;
        HANDLE file = CreateFileA("notifications.log", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL, OPEN_ALWAYS, 0, NULL);
        assert(file != INVALID_HANDLE_VALUE);
        char record[4096];
        int size = snprintf(record, sizeof(record), "%s %s %s\n", argv[2], argv[3], argv[4]);
        DWORD written;
        assert(WriteFile(file, record, (DWORD)size, &written, NULL));
        CloseHandle(file);
        return 0;
    }

    char path[MAX_PATH], helper[MAX_PATH];
    assert(GetModuleFileNameA(NULL, path, sizeof(path)));
    strcpy(helper, path);
    char* filename = strrchr(helper, '\\');
    assert(filename);
    strcpy(filename + 1, "BigBrother Notifications.exe");
    assert(CopyFileA(path, helper, FALSE));
    *strrchr(path, '\\') = 0;
    assert(SetCurrentDirectoryA(path));
    DeleteFileA("notifications.log");
    bb_notifications_set_error_handler(capture_error);
    assert(bb_notify(NULL, "invalid") != 0 && GetLastError() == ERROR_INVALID_PARAMETER);
    assert(bb_notify("invalid", "\xff") != 0 && GetLastError() == ERROR_INVALID_PARAMETER);
    assert(ini_set_string("filtration", "auto_disable", "1") == 0);

    ready = CreateEvent(NULL, TRUE, FALSE, NULL);
    assert(CreateThread(NULL, 0, firewall, NULL, 0, NULL));
    assert(WaitForSingleObject(ready, 5000) == WAIT_OBJECT_0);
    assert(DaemonGetFiltration() == 0);
    assert(DaemonGetFiltration() == 0);
    filtration = 1;
    assert(DaemonGetFiltration() == 1);
    SetServerSessionActive(1);
    SetServerSessionActive(1);
    SetServerSessionActive(0);
    SetServerSessionActive(0);
    char response[128];
    assert(DaemonSetWhitelist("example.com", 11, response, sizeof(response)) == 0);
    assert(DaemonSetWhitelist("FAIL", 4, response, sizeof(response)) != 0);
    assert(DaemonReloadWhitelist(response, sizeof(response)) == 0);
    DWORD started = GetTickCount();
    while (notice_count() < 6 && GetTickCount() - started < 5000) Sleep(10);
    assert(notice_count() == 6);
    assert(bb_notify("FAIL_DELIVERY", "error propagation") == 0);
    started = GetTickCount();
    while (!delivery_error && GetTickCount() - started < 5000) Sleep(10);
    assert(delivery_error == ERROR_ACCESS_DENIED);
    Sleep(100);
    assert(notice_count() == 6);
    puts("PASS: C API, backend notifications without frontend, duplicate suppression, rejected whitelist, and delivery failures");
    return 0;
}
