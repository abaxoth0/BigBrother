#include <assert.h>
#include <stdlib.h>
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "../include/common.h"

SERVICE_STATUS g_ServiceStatus = {0};
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE g_ServiceStopEvent = INVALID_HANDLE_VALUE;

StringView g_FilterExpr = {0};

#define STATUS_OK 0
#define STATUS_INITIALIZATION_FAILED 10
#define STATUS_FAILED_TO_READ_WHITELIST 11

// TODO Idealy need to get rid of this error and handle all the cases where it was used correctly
#define STATUS_UNSPECIFIED_ERROR -1

// #define DEFAULT_WHITELIST_PATH "C:\\ProgramData\\BigBrother\\whitelist.txt"
#define DEFAULT_WHITELIST_PATH "whitelist.txt"

int LoadWhiteList(char* path) {
    if (!path) path = DEFAULT_WHITELIST_PATH;
    FILE *f = fopen(path, "r");
    if (!f) {
        return STATUS_FAILED_TO_READ_WHITELIST;
    }

    StringView line = NewStringView(NULL, 64);
    g_FilterExpr = NewStringView(NULL, 4096);
    while (fgets(line.elems, line.cap, f)) {
        line.elems[strcspn(line.elems, "\n")] = 0;
        if (line.elems[0] == '#' || line.elems[0] == '\0') continue;
        if (g_FilterExpr.elems[0] != '\0') {
            StringViewAppend(&g_FilterExpr, " or ");
        }
        StringViewAppendV(&g_FilterExpr, "dst host ", line.elems, NULL);
    }

    StringViewFree(&line);
    fclose(f);
    if (errno != 0) printf("[ ERROR ] Failed to close file");

    return STATUS_OK;
}

// StringView GetDevInfo(pcap_if_t *dev) {
//     StringView str = NewStringView(NULL, 0);
//
//     StringViewAppendV(&str, dev->description, ": ", NULL);
//     if (strlen(dev->addresses->addr->sa_data)){
//         StringViewAppendV(&str, dev->addresses->addr->sa_data, str, " - ", NULL);
//     }
//     StringViewAppend(&str, dev->name);
//
//     return str;
// }


DWORD WINAPI ServiceWorkerThread(LPVOID lpParam) {
    // if (InitPacketFilter() != 0) {
    //     printf("[ ERROR ] Failed to initialize packet filter");
    //     return STATUS_OK; // Service stays alive
    // }

    // Keep service alive, it is idle - filter is active in kernel
    while(WaitForSingleObject(g_ServiceStopEvent, 0) != WAIT_OBJECT_0) {
        Sleep(1000);
    }

    // if (g_Handle) {
    //     pcap_close(g_Handle);
    //     g_Handle = NULL;
    // }

    return STATUS_OK;
}

void WINAPI ServiceControlHandler(DWORD CtrlCode) {
    switch (CtrlCode) {
        case SERVICE_CONTROL_STOP:
            if (g_ServiceStatus.dwCurrentState != SERVICE_RUNNING) break;

            g_ServiceStatus.dwControlsAccepted = 0;
            g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
            g_ServiceStatus.dwWaitHint = 2000;
            SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

            SetEvent(g_ServiceStopEvent);
            break;
        default:
            break;
    }
}

#define SERVICE_NAME "BigBrother"
#define UpdateServiceStatus() SetServiceStatus(g_StatusHandle, &g_ServiceStatus)

void WINAPI ServiceMain(DWORD argc, LPTSTR *argv) {
    g_StatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceControlHandler);
    if (!g_StatusHandle) return;

    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_ServiceStatus.dwWin32ExitCode = NO_ERROR;
    g_ServiceStatus.dwServiceSpecificExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 1;
    g_ServiceStatus.dwWaitHint = 5000;
    UpdateServiceStatus();

    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_ServiceStopEvent) {
        // TODO move this to function/macro
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        UpdateServiceStatus();
        return;
    }

    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwCheckPoint = 0;
    g_ServiceStatus.dwWaitHint = 0;
    UpdateServiceStatus();

    HANDLE hThread = CreateThread(NULL, 0, ServiceWorkerThread, NULL, 0, NULL);
    if (!hThread) {
        CloseHandle(g_ServiceStopEvent);
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        UpdateServiceStatus();
        return;
    }

    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    CloseHandle(g_ServiceStopEvent);

    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    UpdateServiceStatus();
}

int main() {
    // if (pcap_init(PCAP_CHAR_ENC_UTF_8, g_ErrBuf) != 0) {
    //     printf("[ ERROR ] Failed to initialize pcap librarly: %s\n", g_ErrBuf);
    //     return STATUS_INITIALIZATION_FAILED;
    // };
    SERVICE_TABLE_ENTRY ServiceTable[] = {
        {SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)ServiceMain},
        {NULL, NULL}
    };
    int errc;
    // TODO pass path from args
    if ((errc = LoadWhiteList(NULL)) != STATUS_OK) {
        printf("[ ERROR ] Failed to load while list\n");
        return errc;
    }
    printf("whitelist:\n%s\n", g_FilterExpr.elems);
    // if (InitPacketFilter() != STATUS_OK) {
    //     return STATUS_UNSPECIFIED_ERROR;
    // }
    // printf("Starting service\n");
    // if (!StartServiceCtrlDispatcher(ServiceTable)) {
    //     return STATUS_UNSPECIFIED_ERROR;
    // }
    printf("Stop\n");
    return 0;
}
