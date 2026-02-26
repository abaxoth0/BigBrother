#include <assert.h>
#include <errhandlingapi.h>
#include <handleapi.h>
#include <stdlib.h>
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <windivert.h>
#include <winsvc.h>
#include <inttypes.h>
#include <ws2tcpip.h>
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

#define PACKET_SIZE WINDIVERT_MTU_MAX

char* NewPacketBuffer() {
    char* packet = malloc(PACKET_SIZE);
    // TODO handle this properly somehow
    if (!packet) {
        assert(0 && "memory allocation failed");
    }
    return packet;
}

int IsAllowed(const char* dest_ip) {
    // TODO: Implement. Currently just blocks all
    return 0;
}

// Main thread
DWORD WINAPI FirewallServiceThread(LPVOID lpParam) {
    WINDIVERT_ADDRESS addr;
    UINT recv_len;
    char* packet = NewPacketBuffer();
    HANDLE handle = WinDivertOpen("ip", WINDIVERT_LAYER_NETWORK, 0, 0);

    if (handle == INVALID_HANDLE_VALUE) {
        printf("[ ERROR ] Failed to open WinDivert handle\n");
        return STATUS_UNSPECIFIED_ERROR;
    }

    UINT32 *d = addr.Socket.RemoteAddr;
    StringView dest_ip = NewStringView(NULL, 0);

    while(WaitForSingleObject(g_ServiceStopEvent, 0) != WAIT_OBJECT_0) {
        if (!WinDivertRecv(handle, packet, PACKET_SIZE, &recv_len, &addr)) {
            continue;
        }

        StringViewClear(&dest_ip);
        dest_ip.len = (size_t)sprintf("%du.%du.%du.%du", dest_ip.elems, d[0], d[1], d[2], d[3]);

        if (!IsAllowed(dest_ip.elems)) {
            printf("Blocked packet to %s\n", dest_ip.elems);
            continue;
        }

        WinDivertSend(handle, packet, recv_len, NULL, &addr);
    }

    free(packet);
    StringViewFree(&dest_ip);
    WinDivertClose(handle);

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

    HANDLE hThread = CreateThread(NULL, 0, FirewallServiceThread, NULL, 0, NULL);
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
    printf("STARING\n");
    WINDIVERT_ADDRESS addr;
    UINT recv_len;
    char* packet = NewPacketBuffer();
    HANDLE handle = WinDivertOpen("udp.SrcPort == 53 or (outbound and ip)", WINDIVERT_LAYER_NETWORK, 0, 0);

    if (handle == INVALID_HANDLE_VALUE) {
        printf("[ ERROR ] Failed to open WinDivert handle. Error code: %lu\n", GetLastError());
        return STATUS_UNSPECIFIED_ERROR;
    }

    PWINDIVERT_IPHDR ip_hdr;
    PWINDIVERT_TCPHDR tcp_hdr;
    void* payload = NULL;
    UINT* payload_len = NULL;

    printf("Firewall started\n");

    while(1) {
        if (!WinDivertRecv(handle, packet, PACKET_SIZE, &recv_len, &addr)) {
            continue;
        }
        if (ip_hdr == NULL) {
            WinDivertSend(handle, packet, recv_len, NULL, &addr);
        }

        WinDivertHelperParsePacket(
            packet, PACKET_SIZE,
            &ip_hdr,
            NULL, // Ignore IPv6
            NULL, // Ignore protocol
            NULL, NULL, // Ignor ICMP (TODO: can be usefull for debug)
            &tcp_hdr,
            NULL, // Ignore UDP
            payload, payload_len,
            NULL, NULL // Ignore next package
        );

        char src[16], dst[16];
        inet_ntop(AF_INET, &ip_hdr->SrcAddr, src, sizeof(src));
        inet_ntop(AF_INET, &ip_hdr->DstAddr, dst, sizeof(dst));

        printf("[IP] %s -> %s | protocol: %u\n", src, dst, ip_hdr->Protocol);

        // if (!IsAllowed(dest_ip.elems)) {
        //     printf("Blocked packet to %s\n", dest_ip.elems);
        //     continue;
        // }

        WinDivertSend(handle, packet, recv_len, NULL, &addr);
    }
    printf("STARING\n");
}

int main2() {
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
    if (!StartServiceCtrlDispatcher(ServiceTable)) {
        printf("[ ERROR ] Failed to start firewall service\n");
        return STATUS_UNSPECIFIED_ERROR;
    }
    printf("Stop\n");
    return 0;
}
