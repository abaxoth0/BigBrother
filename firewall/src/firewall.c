/** @file firewall.c
 * @brief Main firewall service implementation using WinDivert.
 */

#include <assert.h>
#include <errhandlingapi.h>
#include <handleapi.h>
#include <libloaderapi.h>
#include <minwindef.h>
#include <stdlib.h>
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <windivert.h>
#include <winsvc.h>
#include <inttypes.h>
#include <ws2tcpip.h>
#include "../include/common.h"
#include "../include/allowlist.h"
#include "../include/dns.h"

SERVICE_STATUS g_ServiceStatus = {0};
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE g_ServiceStopEvent = INVALID_HANDLE_VALUE;

Whitelist g_Whitelist = {0};
IpAllowlist g_IpAllowlist = {0};

StringView g_FilterExpr = {0};

#define STATUS_OK 0
#define STATUS_INITIALIZATION_FAILED 10
#define STATUS_FAILED_TO_READ_WHITELIST 11
#define STATUS_FAILED_TO_START_SERVICE 12

#define STATUS_UNSPECIFIED_ERROR -1

// TODO: Allow user to specify whitelist path
#define DEFAULT_WHITELIST_PATH "C:\\Users\\user\\Desktop\\build\\whitelist.txt"

#define DPRINTF_BUF_SIZE 2048
static char dprintf_buf[DPRINTF_BUF_SIZE];

#define DPRINTF(...) do {               \
    sprintf(dprintf_buf, __VA_ARGS__);   \
    OutputDebugString(dprintf_buf);      \
} while(0)

/**
 * @brief Load whitelist domains and IPs from file.
 *
 * Reads entries from a text file (one per line, lines starting
 * with # are comments). Supports both:
 * - Plain IPs (e.g., "1.2.3.4") - added directly to allowlist
 * - Domain names (e.g., "example.com") - added to domain whitelist
 *
 * @param[in] path Path to whitelist file. If NULL, uses default path.
 *
 * @return STATUS_OK on success, STATUS_FAILED_TO_READ_WHITELIST on failure.
 */
int LoadWhiteList(char* path) {
    if (!path) path = DEFAULT_WHITELIST_PATH;
    DPRINTF("[INFO] Reading whitelist at: %s\n", path);
    FILE *f = fopen(path, "r");
    if (!f) {
        return STATUS_FAILED_TO_READ_WHITELIST;
    }

    WhitelistInit(&g_Whitelist);
    IpAllowlistInit(&g_IpAllowlist);

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        if (line[0] == '#' || line[0] == '\0') continue;

        struct in_addr addr;
        if (inet_pton(AF_INET, line, &addr) == 1) {
            IpAllowlistAdd(&g_IpAllowlist, addr.s_addr, line, 0);
            DPRINTF("[INFO] Added IP to allowlist: %s\n", line);

        } else {
            WhitelistAdd(&g_Whitelist, line);
        }
    }

    fclose(f);
    DPRINTF("[INFO] Loaded %zu whitelisted domains and %zu IPs\n", g_Whitelist.count, g_IpAllowlist.count);

    return STATUS_OK;
}

#define PACKET_SIZE WINDIVERT_MTU_MAX

/**
 * @brief Allocate a new packet buffer.
 *
 * @return Pointer to allocated buffer, or asserts on failure.
 */
char* NewPacketBuffer() {
    char* packet = malloc(PACKET_SIZE);
    if (!packet) {
        assert(0 && "memory allocation failed");
    }
    return packet;
}

/**
 * @brief Check if destination IP is allowed.
 *
 * @param[in] dest_ip Destination IPv4 address (network byte order).
 *
 * @return Non-zero if allowed, zero if blocked.
 */
int IsAllowed(uint32_t dest_ip) {
    return IpAllowlistContains(&g_IpAllowlist, dest_ip);
}

#define WINDIVERT_FILTER "ip"
#define PACKET_PAYLOAD_SIZE 1500 // Ethernet MTU

/**
 * @brief Main firewall service thread.
 *
 * Opens WinDivert handle and processes network packets,
 * filtering based on whitelist and IP allowlist.
 *
 * @param[in] lpParam Unused thread parameter.
 *
 * @return Thread exit status.
 */
DWORD WINAPI FirewallServiceThread(LPVOID lpParam) {
    WINDIVERT_ADDRESS addr;
    UINT recv_len;
    char* packet = NewPacketBuffer();
    HANDLE handle = WinDivertOpen(WINDIVERT_FILTER, WINDIVERT_LAYER_NETWORK, 0, 0);

    if (handle == INVALID_HANDLE_VALUE) {
        DPRINTF("[ ERROR ] Failed to open WinDivert handle. Error code: %lu\n", GetLastError());
        return STATUS_UNSPECIFIED_ERROR;
    }

    PWINDIVERT_IPHDR ip_hdr = NULL;
    PWINDIVERT_TCPHDR tcp_hdr = NULL;
    PWINDIVERT_UDPHDR udp_hdr = NULL;

    // Currently it's used only for DNS payloads which usually < 512 bytes.
    // Consider make it a dynamic array if you will need to get payload from other protocols.
    char payload_buf[PACKET_PAYLOAD_SIZE];
    void* payload_ptr = payload_buf;
    UINT payload_len = 0;

    OutputDebugString("Firewall started\n");

    while(WaitForSingleObject(g_ServiceStopEvent, 0) != WAIT_OBJECT_0) {
        if (!WinDivertRecv(handle, packet, PACKET_SIZE, &recv_len, &addr)) {
            continue;
        }

        ip_hdr = NULL;
        tcp_hdr = NULL;
        udp_hdr = NULL;

        BOOL ok = WinDivertHelperParsePacket(
            packet, recv_len,
            &ip_hdr,
            NULL,
            NULL,
            NULL, NULL,
            &tcp_hdr,
            &udp_hdr,
            (void*)&payload_ptr, &payload_len,
            NULL, NULL
        );

        if (!ok || ip_hdr == NULL) {
            WinDivertSend(handle, packet, recv_len, NULL, &addr);
            continue;
        }

        char src[16], dst[16];
        inet_ntop(AF_INET, &ip_hdr->SrcAddr, src, sizeof(src));
        inet_ntop(AF_INET, &ip_hdr->DstAddr, dst, sizeof(dst));

#ifdef DEBUG
        if (udp_hdr) {
            DPRINTF("[UDP] %s -> %s | src: %u; dst: %u | payload: %u\n",
                    src, dst, ntohs(udp_hdr->SrcPort), ntohs(udp_hdr->DstPort), payload_len);
        } else if (tcp_hdr) {
            DPRINTF("[TCP] %s -> %s | src: %u; dst: %u | payload: %u\n",
                    src, dst, ntohs(tcp_hdr->SrcPort), ntohs(tcp_hdr->DstPort), payload_len);
        }
#endif

        if (udp_hdr && (ntohs(udp_hdr->DstPort) == 53 || ntohs(udp_hdr->SrcPort) == 53)) {
            // Not char* cuz DNS packets contain binary data, not a null-terminated strings
            uint8_t* dns_data = (uint8_t*)payload_ptr;
            size_t dns_len = payload_len;

#ifdef DEBUG
            DPRINTF("[DNS-PORT] Detected DNS packet, src: %u, dst: %u, dns_len: %zu\n",
                   ntohs(udp_hdr->SrcPort), ntohs(udp_hdr->DstPort), dns_len);
#endif

            if (dns_len >= DNS_MIN_REQ_LEN) {
                DnsPacket dns = DnsParse(dns_data, dns_len);

#ifdef DEBUG
            DPRINTF("[DNS] valid: %d, domain: '%s', response: %s, answers: %u\n",
                       dns.is_valid, dns.question.domain,
                       dns.is_response ? "yes" : "no", dns.answer_count);
#endif

                if (!dns.is_valid || dns.question.domain[0] == '\0') {
                    goto filtering;
                }

                // Only process DNS responses (not queries) with answer records
                if (!dns.is_response || dns.answer_count == 0) {
                    goto filtering;
                }

                int domain_whitelisted = 0;
                for (size_t w = 0; w < g_Whitelist.count; w++) {
                    if (DnsCheckDomain(dns.question.domain, g_Whitelist.entries[w].domain)) {
                        domain_whitelisted = 1;
                        break;
                    }
                }

                // Add IPs to allowlist if domain is whitelisted
                for (uint32_t i = 0; i < dns.answer_count; i++) {
                    for (uint32_t j = 0; j < dns.answers[i].ip_count; j++) {
                        uint32_t resolved_ip = dns.answers[i].ips[j];
                        struct in_addr addr_ip = { .s_addr = resolved_ip };

                        if (domain_whitelisted) {
                            IpAllowlistAdd(&g_IpAllowlist, resolved_ip, dns.question.domain, dns.answers[i].ttl);
                        }

#ifdef DEBUG
                        DPRINTF("[DNS-RESPONSE] %s -> %s (whitelisted: %d)\n",
                                dns.question.domain, inet_ntoa(addr_ip), domain_whitelisted);
#endif
                    }
                }

            filtering:
                DnsFree(&dns);
            }
        }

        // Extract source and destination IPs
        uint32_t src_ip = ip_hdr->SrcAddr;
        uint32_t dest_ip = ip_hdr->DstAddr;

        /* Check if source/destination is a local IP address.
         * Local IPs must not be blocked:
         *   - 127.x.x.x (loopback)
         *   - 192.168.x.x (private Class C)
         *   - 10.x.x.x (private Class A)
         *   - 172.(16-31).x.x (private Class B) */
        int is_local_src = ((src_ip & 0xFF000000) == 0x7F000000) ||
            ((src_ip & 0xFFF00000) == 0xAC100000) || ((src_ip & 0xFFFF0000) == 0xC0A80000) ||
            ((src_ip & 0xFF000000) == 0x0A000000);
        int is_local_dst = ((dest_ip & 0xFF000000) == 0x7F000000) ||
            ((dest_ip & 0xFFF00000) == 0xAC100000) || ((dest_ip & 0xFFFF0000) == 0xC0A80000) ||
            ((dest_ip & 0xFF000000) == 0x0A000000);
        int is_local = is_local_src || is_local_dst;

        /*
         * Blocking logic:
         * - Only block outbound packets (inbound are always allowed)
         * - Allow DNS queries (UDP port 53) so we can resolve domains
         * - Allow packets to local IP ranges (127.x.x.x, 192.168.x.x, 10.x.x.x, 172.16-31.x.x)
         * - Allow packets to IPs that were resolved from whitelisted domains
         * - Block everything else
         */
        if (addr.Outbound && !IsAllowed(dest_ip) && !is_local) {
            int is_dns = (udp_hdr && (ntohs(udp_hdr->DstPort) == 53));
            if (is_dns) {
                WinDivertSend(handle, packet, recv_len, NULL, &addr);
                continue;
            }

            const char* domain = IpAllowlistGetDomain(&g_IpAllowlist, dest_ip);
            int domain_whitelisted = 0;
            if (domain && domain[0]) {
                for (size_t w = 0; w < g_Whitelist.count; w++) {
                    if (DnsCheckDomain(domain, g_Whitelist.entries[w].domain)) {
                        domain_whitelisted = 1;
                        break;
                    }
                }
            }

            if (!domain_whitelisted) {
#ifdef DEBUG
                DPRINTF("[BLOCKED] Packet to %s blocked", dst);
                if (domain) {
                    DPRINTF(" (domain: %s not whitelisted)", domain);
                }
                DPRINTF("\n");
#endif
                continue;
            }
        }

        WinDivertSend(handle, packet, recv_len, NULL, &addr);

    }

    free(packet);
    WinDivertClose(handle);

    return STATUS_OK;
}

/**
 * @brief Service control handler for Windows Service.
 *
 * Handles service control messages (primarily stop request).
 *
 * @param[in] CtrlCode Control code from service manager.
 */
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

/**
 * @brief Starts service.
 *
 * Initializes the Windows service, loads whitelist, and starts
 * the firewall service thread.
 *
 * @param[in] argc Argument count.
 * @param[in] argv Argument vector.
 */
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

/**
 * @brief App entry point.
 *
 * @return Exit code.
 */
int main(int argc, char** argv) {
    OutputDebugString("STARTING\n");
    int err = LoadWhiteList(NULL);
    if (err) {
        DPRINTF("[INFO] Failed to load whitelist: error #%d\n", err);
        return err;
    };
    DPRINTF("[INFO] Whitelist loaded with %zu domains\n", g_Whitelist.count);

    SERVICE_TABLE_ENTRY serviceTable[] = {
        {SERVICE_NAME, ServiceMain},
        {NULL, NULL}
    };
    if (!StartServiceCtrlDispatcher(serviceTable)) {
        return STATUS_FAILED_TO_START_SERVICE;
    }
}
