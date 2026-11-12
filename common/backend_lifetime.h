#ifndef BB_BACKEND_LIFETIME_H
#define BB_BACKEND_LIFETIME_H

#include <windows.h>

static inline HANDLE bb_open_backend_process(void) {
    SC_HANDLE manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!manager) return NULL;
    SC_HANDLE service = OpenServiceW(manager, L"BigBrother Client", SERVICE_QUERY_STATUS);
    SERVICE_STATUS_PROCESS status;
    DWORD bytes = 0;
    HANDLE process = NULL;
    if (service && QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, (BYTE*)&status, sizeof(status), &bytes) &&
        status.dwCurrentState == SERVICE_RUNNING && status.dwProcessId)
        process = OpenProcess(SYNCHRONIZE, FALSE, status.dwProcessId);
    if (service) CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return process;
}

static inline DWORD WINAPI bb_watch_backend(LPVOID stop_event) {
    HANDLE backend = bb_open_backend_process();
    if (!backend) {
        SetEvent((HANDLE)stop_event);
        return 1;
    }
    HANDLE handles[] = {(HANDLE)stop_event, backend};
    while (WaitForMultipleObjects(2, handles, FALSE, 250) == WAIT_TIMEOUT) {
        SC_HANDLE manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
        SC_HANDLE service = manager ? OpenServiceW(manager, L"BigBrother Client", SERVICE_QUERY_STATUS) : NULL;
        SERVICE_STATUS status;
        BOOL running = service && QueryServiceStatus(service, &status) && status.dwCurrentState == SERVICE_RUNNING;
        if (service) CloseServiceHandle(service);
        if (manager) CloseServiceHandle(manager);
        if (!running) break;
    }
    SetEvent((HANDLE)stop_event);
    CloseHandle(backend);
    return 0;
}

#endif
