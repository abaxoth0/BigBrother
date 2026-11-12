#include <assert.h>
#include <windows.h>
#include <stdio.h>

static volatile LONG backend_state = SERVICE_RUNNING;
static HANDLE backend_exit;
static int unavailable;

static SC_HANDLE WINAPI mock_manager(LPCWSTR machine, LPCWSTR database, DWORD access) {
    (void)machine; (void)database; (void)access;
    return (SC_HANDLE)1;
}
static SC_HANDLE WINAPI mock_service(SC_HANDLE manager, LPCWSTR name, DWORD access) {
    (void)manager; (void)access;
    assert(wcscmp(name, L"BigBrother Client") == 0);
    return unavailable ? NULL : (SC_HANDLE)2;
}
static BOOL WINAPI mock_query(SC_HANDLE service, SERVICE_STATUS* status) {
    (void)service;
    memset(status, 0, sizeof(*status));
    status->dwCurrentState = (DWORD)backend_state;
    return TRUE;
}
static BOOL WINAPI mock_query_ex(SC_HANDLE service, SC_STATUS_TYPE type, BYTE* bytes, DWORD size, DWORD* needed) {
    (void)service; (void)type; (void)size;
    SERVICE_STATUS_PROCESS* status = (SERVICE_STATUS_PROCESS*)bytes;
    memset(status, 0, sizeof(*status));
    status->dwCurrentState = (DWORD)backend_state;
    status->dwProcessId = 123;
    *needed = sizeof(*status);
    return TRUE;
}
static HANDLE WINAPI mock_process(DWORD access, BOOL inherit, DWORD pid) {
    (void)access; (void)inherit;
    assert(pid == 123);
    HANDLE copy;
    assert(DuplicateHandle(GetCurrentProcess(), backend_exit, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS));
    return copy;
}
static BOOL WINAPI mock_close(SC_HANDLE service) { (void)service; return TRUE; }

#define OpenSCManagerW mock_manager
#define OpenServiceW mock_service
#define QueryServiceStatus mock_query
#define QueryServiceStatusEx mock_query_ex
#define OpenProcess mock_process
#define CloseServiceHandle mock_close
#include "../../common/backend_lifetime.h"

int main(void) {
    backend_exit = CreateEventW(NULL, TRUE, FALSE, NULL);
    for (int scenario = 0; scenario < 4; ++scenario) {
        backend_state = SERVICE_RUNNING;
        unavailable = scenario == 2;
        ResetEvent(backend_exit);
        HANDLE stop = CreateEventW(NULL, TRUE, FALSE, NULL);
        HANDLE thread = CreateThread(NULL, 0, bb_watch_backend, stop, 0, NULL);
        assert(thread);
        if (!unavailable) {
            Sleep(100);
            assert(WaitForSingleObject(stop, 0) == WAIT_TIMEOUT);
            if (scenario == 0) backend_state = SERVICE_STOP_PENDING;
            else if (scenario == 1) SetEvent(backend_exit);
            else SetEvent(stop);
        }
        assert(WaitForSingleObject(stop, 2000) == WAIT_OBJECT_0);
        assert(WaitForSingleObject(thread, 2000) == WAIT_OBJECT_0);
        CloseHandle(thread);
        CloseHandle(stop);
    }
    CloseHandle(backend_exit);
    puts("PASS: firewall stops for backend stop, crash, unavailable service, and its own stop");
}
