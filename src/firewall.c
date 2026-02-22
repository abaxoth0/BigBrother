#include <assert.h>
#include <stdlib.h>
#include <pcap/pcap.h>
#include <winsock2.h>
#include <windows.h>
#include <pcap.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "../include/common.h"

SERVICE_STATUS g_ServiceStatus = {0};
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE g_ServiceStopEvent = INVALID_HANDLE_VALUE;

pcap_t *g_Handle = NULL;
char g_ErrBuf[PCAP_ERRBUF_SIZE];

#define MAX_FILTER_LEN 1024
char g_FilterExpr[MAX_FILTER_LEN] = "";

#define STATUS_OK 0
#define STATUS_INITIALIZATION_FAILED 10
#define STATUS_FAILED_TO_READ_WHITELIST 11

// TODO Idealy need to get rid of this error and handle all the cases where it was used correctly
#define STATUS_UNSPECIFIED_ERROR -1

#define DEFAULT_WHITELIST_PATH "C:\\ProgramData\\BigBrother\\whitelist.txt"

int LoadWhiteList(char* path) {
    if (!path) path = DEFAULT_WHITELIST_PATH;
    FILE *f = fopen(path, "r");
    if (!f) {
        return STATUS_FAILED_TO_READ_WHITELIST;
    }

    char line[MAX_FILTER_LEN];
    g_FilterExpr[0] = '\0';
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        if (line[0] == '#' || line[0] == '\0') continue;

        if (g_FilterExpr[0] != '\0') strcat(g_FilterExpr, " or ");

        strcat(g_FilterExpr, "dst host ");
        strcat(g_FilterExpr, line);
    }

    fclose(f);
    if (errno != 0) printf("[ ERROR ] Failed to close file");

    return STATUS_OK;
}

int InitPacketFilter(void) {
    pcap_if_t *alldevs, *dev;
    char errbuf[PCAP_ERRBUF_SIZE];

    if (pcap_findalldevs(&alldevs, errbuf) == -1) {
        return STATUS_UNSPECIFIED_ERROR;
    }

    // Use first non-loopback device
    for (dev = alldevs; dev; dev = dev->next) {
        if (dev->addresses && !(dev->flags&PCAP_IF_LOOPBACK)){
            break;
        }
    }
    if (!dev) {
        pcap_freealldevs(alldevs);
        return STATUS_UNSPECIFIED_ERROR;
    }

    g_Handle = pcap_open_live(dev->name, 1<<16, 1, 1000, errbuf);
    pcap_freealldevs(alldevs);
    if (!g_Handle) return STATUS_UNSPECIFIED_ERROR;

    int errc;
    // TODO pass path from args
    if ((errc = LoadWhiteList(NULL)) != STATUS_OK) {
        printf("[ ERROR ] Failed to load while list");
        return errc;
    }

    struct bpf_program fp;
    if (pcap_compile(g_Handle, &fp, g_FilterExpr, 1, PCAP_NETMASK_UNKNOWN) == -1) {
        // TODO move this 2 lines into a function/macro
        pcap_close(g_Handle);
        g_Handle = NULL;
        return STATUS_UNSPECIFIED_ERROR;
    }
    if (pcap_setfilter(g_Handle, &fp) == -1) {
        pcap_freecode(&fp);
        pcap_close(g_Handle);
        g_Handle = NULL;
        return STATUS_UNSPECIFIED_ERROR;
    }

    pcap_freecode(&fp);

    return STATUS_OK;
}

DWORD WINAPI ServiceWorkerThread(LPVOID lpParam) {
    if (InitPacketFilter() != 0) {
        printf("[ ERROR ] Failed to initialize packet filter");
        return STATUS_OK; // Service stays alive
    }

    // Keep service alive, it is idle - filter is active in kernel
    while(WaitForSingleObject(g_ServiceStopEvent, 0) != WAIT_OBJECT_0) {
        Sleep(1000);
    }

    if (g_Handle) {
        pcap_close(g_Handle);
        g_Handle = NULL;
    }

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
    StringView str = {0};
    int n = 128;
    DA_GROW(&str, n);
}

int main2() {
    if (pcap_init(PCAP_CHAR_ENC_UTF_8, g_ErrBuf) != 0) {
        printf("[ ERROR ] Failed to initialize pcap librarly: %s\n", g_ErrBuf);
        return STATUS_INITIALIZATION_FAILED;
    };
    SERVICE_TABLE_ENTRY ServiceTable[] = {
        {SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)ServiceMain},
        {NULL, NULL}
    };
    if (LoadWhiteList("whitelist.txt") != STATUS_OK) {
        printf("[ ERROR ] Failed to load whitelist\n");
        return STATUS_FAILED_TO_READ_WHITELIST;
    }
    printf("whitelist:\n%s\n", g_FilterExpr);
    // printf("Starting service\n");
    // if (!StartServiceCtrlDispatcher(ServiceTable)) {
    //     return STATUS_UNSPECIFIED_ERROR;
    // }
    printf("Stop\n");
    return 0;
}
