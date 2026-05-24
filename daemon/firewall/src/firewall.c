/** @file firewall.c
 * @brief Main firewall service implementation using WinDivert.
 */

#include <assert.h>
#include <errhandlingapi.h>
#include <handleapi.h>
#include <libloaderapi.h>
#include <minwindef.h>
#include <stdarg.h>
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
#include "../include/ipc.h"
#include "../../common/log/log.h"

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
static char g_WhitelistPath[MAX_PATH] = "whitelist.txt";
static char g_ConfigPath[MAX_PATH] = "config\\config.ini";
static char g_ServerIp[64] = {0};
static char g_ClientExePath[MAX_PATH] = {0};
static DWORD g_ClientPid = 0;
static HANDLE g_ClientProcess = NULL;
static HANDLE g_ClientStopEvent = NULL;

static void get_exe_path(char* buf, size_t size) {
    char* name = buf + GetModuleFileName(NULL, buf, (DWORD)size);
    // Find last backslash
    while (name > buf && *(name - 1) != '\\') name--;
    *name = '\0';
}

static void init_paths(void) {
    get_exe_path(g_ClientExePath, sizeof(g_ClientExePath));
    snprintf(g_WhitelistPath, sizeof(g_WhitelistPath), "%s\\whitelist.txt", g_ClientExePath);
    snprintf(g_ConfigPath, sizeof(g_ConfigPath), "%s\\config\\config.ini", g_ClientExePath);
}

static void init_logging(void) {
    char logs_dir[LOG_PATH_MAX];
    snprintf(logs_dir, sizeof(logs_dir), "%s\\logs", g_ClientExePath);
    CreateDirectory(logs_dir, NULL);

    char log_path[LOG_PATH_MAX];
    snprintf(log_path, sizeof(log_path), "%s\\firewall.binlog", logs_dir);
    
    // Set log path in global context
    extern LoggerContext* g_logger;
    if (g_logger) {
        snprintf(g_logger->log_path, sizeof(g_logger->log_path), "%s", log_path);
        g_logger->max_file_size = 10 * 1024;  // 10KB for testing
        g_logger->max_files = LOG_MAX_FILES;
    }
    
    FILE* f = fopen(log_path, "a");
    if (!f) {
        char temp_path[LOG_PATH_MAX];
        GetTempPath(sizeof(temp_path), temp_path);
        snprintf(log_path, sizeof(log_path), "%sBigBrother_firewall.binlog", temp_path);
        if (g_logger) {
            snprintf(g_logger->log_path, sizeof(g_logger->log_path), "%s", log_path);
        }
        f = fopen(log_path, "a");
    }
    
    // Legacy - still needed for sync writes if needed
    if (f) {
        LogFile = f;
    }
}

static void ensure_config_dir(const char* full_path) {
    char dir[MAX_PATH];
    strncpy(dir, full_path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char* p = strrchr(dir, '\\');
    if (p) {
        *p = '\0';
        CreateDirectory(dir, NULL);
    }
}

static void load_config(const char* path) {
    get_exe_path(g_ClientExePath, sizeof(g_ClientExePath));
    strncat(g_ClientExePath, "\\bb-client.exe", sizeof(g_ClientExePath) - strlen(g_ClientExePath) - 1);
    LOGF("[Config] Default client exe: %s", g_ClientExePath);

    ensure_config_dir(path);

    FILE* f = fopen(path, "r");
    if (!f) {
        // Create default config file
        f = fopen(path, "w");
        if (f) {
            fprintf(f, "; BigBrother configuration file\n");
            fclose(f);
        }
        LOGF("[Config] Created default config file: %s", path);
        return;
    }

    int section_matched = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        char* end = p + strlen(p) - 1;
        while (end > p && (*end == '\n' || *end == '\r' || *end == ' ' || *end == '\t')) *end-- = '\0';
        if (*p == '\0' || *p == ';' || *p == '#') continue;

        if (*p == '[') {
            char* close = strchr(p, ']');
            if (!close) continue;
            *close = '\0';
            section_matched = (_stricmp(p + 1, "server") == 0 || _stricmp(p + 1, "daemon") == 0);
            continue;
        }

        if (!section_matched) continue;

        char* eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = p;
        char* value = eq + 1;
        while (*value == ' ' || *value == '\t') value++;

        if (strcmp(key, "address") == 0 && section_matched) {
            // [server] address
            strncpy(g_ServerIp, value, sizeof(g_ServerIp) - 1);
            LOGF("[Config] Server IP: %s", g_ServerIp);
        } else if (strcmp(key, "client_exe") == 0) {
            strncpy(g_ClientExePath, value, sizeof(g_ClientExePath) - 1);
            LOGF("[Config] Client exe: %s", g_ClientExePath);
        }
    }

    fclose(f);
}

static int spawn_client_backend(void) {
    STARTUPINFO si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    // Re-discover client exe path from own exe directory
    get_exe_path(g_ClientExePath, sizeof(g_ClientExePath));
    strncat(g_ClientExePath, "\\bb-client.exe", sizeof(g_ClientExePath) - strlen(g_ClientExePath) - 1);

    char cmd[512];
    if (g_ServerIp[0] != '\0') {
        snprintf(cmd, sizeof(cmd), "\"%s\" -d %s", g_ClientExePath, g_ServerIp);
    } else {
        snprintf(cmd, sizeof(cmd), "\"%s\" -d", g_ClientExePath);
    }

    LOGF("[ServiceMain] Spawning: %s", cmd);

    if (CreateProcess(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW | INHERIT_PARENT_AFFINITY, NULL, NULL, &si, &pi)) {
        if (g_ClientProcess) {
            CloseHandle(g_ClientProcess);
        }
        g_ClientProcess = pi.hProcess;
        g_ClientPid = pi.dwProcessId;
        CloseHandle(pi.hThread);
        LOGF("[ServiceMain] Client backend started, pid: %lu", g_ClientPid);
        return 0;
    } else {
        LOGF("[ServiceMain] CreateProcess failed: cmd=%s err=%lu", cmd, GetLastError());
        return -1;
    }
}

static void stop_client_backend(void) {
    if (g_ClientProcess) {
        TerminateProcess(g_ClientProcess, 0);
        CloseHandle(g_ClientProcess);
        g_ClientProcess = NULL;
        g_ClientPid = 0;
    }
    if (g_ClientStopEvent) {
        SetEvent(g_ClientStopEvent);
    }
}

static int is_client_running(void) {
    if (!g_ClientProcess || !g_ClientPid) {
        return 0;
    }

    DWORD exit_code;
    if (GetExitCodeProcess(g_ClientProcess, &exit_code) && exit_code == STILL_ACTIVE) {
        return 1;
    }

    return 0;
}

static DWORD WINAPI client_monitor_thread(LPVOID param) {
    while (WaitForSingleObject(g_ClientStopEvent, 5000) != WAIT_OBJECT_0) {
        if (!is_client_running() && g_ServerIp[0] != '\0') {
            LOGF("[ClientMonitor] Client died, restarting...");
            spawn_client_backend();
        }
    }
    return 0;
}

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
    if (!path) path = g_WhitelistPath;
    LOGF("[INFO] Reading whitelist at: %s", path);

    WhitelistInit(&g_Whitelist);
    IpAllowlistInit(&g_IpAllowlist);

    FILE *f = fopen(path, "r");
    if (!f) {
        // Create empty whitelist file so service doesn't fail on missing file
        f = fopen(path, "w");
        if (f) {
            fprintf(f, "; BigBrother whitelist - add domains one per line\n");
            fclose(f);
            LOGF("[INFO] Created empty whitelist file: %s", path);
        } else {
            LOGF("[WARN] Could not create whitelist file: %s, continuing with empty whitelist", path);
        }
        return STATUS_OK;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        // Trim leading whitespace
        char* p = line;
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;

        // Trim trailing whitespace and newline
        size_t len = strlen(p);
        while (len > 0 && (p[len-1] == '\n' || p[len-1] == '\r' || p[len-1] == ' ' || p[len-1] == '\t')) {
            p[--len] = '\0';
        }

        if (p[0] == '#' || p[0] == ';' || p[0] == '\0') continue;

        struct in_addr addr;
        if (inet_pton(AF_INET, p, &addr) == 1) {
            IpAllowlistAdd(&g_IpAllowlist, ntohl(addr.s_addr), p, 0);
            LOGF("[INFO] Added IP to allowlist: %s", p);

        } else {
            WhitelistAdd(&g_Whitelist, p);
        }
    }

    fclose(f);
    LOGF("[INFO] Loaded %zu whitelisted domains and %zu IPs", g_Whitelist.count, g_IpAllowlist.count);

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
#define PACKET_QUEUE_TIMEOUT 500 // ms

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
        LOGE("Failed to open WinDivert handle. Error code: %lu", GetLastError());
        return STATUS_UNSPECIFIED_ERROR;
    }

    if (!WinDivertSetParam(handle, WINDIVERT_PARAM_QUEUE_TIME, PACKET_QUEUE_TIMEOUT)) {
        LOGE("Failed to set packet queue timeout. Error code: %lu", GetLastError());
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

    OutputDebugString("Firewall started");

    while (WaitForSingleObject(g_ServiceStopEvent, 0) != WAIT_OBJECT_0) {
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
            DLOGF("[UDP] %s -> %s | src: %u; dst: %u | payload: %u",
                  src, dst, ntohs(udp_hdr->SrcPort), ntohs(udp_hdr->DstPort), payload_len);
        } else if (tcp_hdr) {
            DLOGF("[TCP] %s -> %s | src: %u; dst: %u | payload: %u",
                  src, dst, ntohs(tcp_hdr->SrcPort), ntohs(tcp_hdr->DstPort), payload_len);
        }
#endif

        if (udp_hdr && (ntohs(udp_hdr->DstPort) == 53 || ntohs(udp_hdr->SrcPort) == 53)) {
            // Not char* cuz DNS packets contain binary data, not a null-terminated strings
            uint8_t* dns_data = (uint8_t*)payload_ptr;
            size_t dns_len = payload_len;

#ifdef DEBUG
            DLOGF("[DNS-PORT] Detected DNS packet, src: %u, dst: %u, dns_len: %zu",
                  ntohs(udp_hdr->SrcPort), ntohs(udp_hdr->DstPort), dns_len);
#endif

            if (dns_len >= DNS_MIN_REQ_LEN) {
                DnsPacket dns = DnsParse(dns_data, dns_len);

#ifdef DEBUG
                DLOGF("[DNS] valid: %d, domain: '%s', response: %s, answers: %u",
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

                        if (domain_whitelisted) {
                            IpAllowlistAdd(&g_IpAllowlist, resolved_ip, dns.question.domain, dns.answers[i].ttl);
                        }

#ifdef DEBUG
                        struct in_addr addr_ip = { .s_addr = htonl(resolved_ip) };
                        DLOGF("[DNS-RESPONSE] %s -> %s (whitelisted: %d)",
                              dns.question.domain, inet_ntoa(addr_ip), domain_whitelisted);
#endif
                    }
                }

            filtering:
                DnsFree(&dns);
            }
        }

        // IP header fields are in network byte order, convert to host for comparisons
        uint32_t src_ip = ntohl(ip_hdr->SrcAddr);
        uint32_t dest_ip = ntohl(ip_hdr->DstAddr);

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
        int is_local = is_local_src && is_local_dst;

        /*
         * Blocking logic:
         * - Only block outbound packets (inbound are always allowed)
         * - Allow DNS queries (UDP port 53) so we can resolve domains
         * - Allow packets to local IP ranges (127.x.x.x, 192.168.x.x, 10.x.x.x, 172.16-31.x.x)
         * - Allow packets to IPs that were resolved from whitelisted domains
         * - Block everything else
         */
        // Track stats for diagnostics
        static int packet_count = 0;
        static int blocked_count = 0;
        packet_count++;

        if (addr.Outbound && !IsAllowed(dest_ip) && !is_local) {
            int is_dns_udp = (udp_hdr && (ntohs(udp_hdr->DstPort) == 53));
            int is_dns_tcp = (tcp_hdr && (ntohs(tcp_hdr->DstPort) == 53));
            if (is_dns_udp || is_dns_tcp) {
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
                blocked_count++;
                if (domain) {
                    LOGB("Packet to %s blocked (domain: %s not whitelisted)", dst, domain);
                } else {
                    LOGB("Packet to %s blocked", dst);
                }
                continue;
            }
        }

        if (packet_count % 100 == 0) {
            DLOGF("[STATS] processed=%d blocked=%d allowlist=%zu whitelist=%zu outbound=%d local=%d",
                  packet_count, blocked_count, g_IpAllowlist.count, g_Whitelist.count,
                  addr.Outbound, is_local);
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
    char buf[128];
    snprintf(buf, sizeof(buf), "[ServiceControlHandler] Received control: %lu", CtrlCode);
    OutputDebugString(buf);

    switch (CtrlCode) {
        case SERVICE_CONTROL_STOP:
            OutputDebugString("[ServiceControlHandler] STOP received");
            if (g_ServiceStatus.dwCurrentState != SERVICE_RUNNING) {
                OutputDebugString("[ServiceControlHandler] Not running, ignoring");
                break;
            }

            g_ServiceStatus.dwControlsAccepted = 0;
            g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
            g_ServiceStatus.dwWaitHint = 2000;
            SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

            OutputDebugString("[ServiceControlHandler] Setting stop event");
            SetEvent(g_ServiceStopEvent);
            OutputDebugString("[ServiceControlHandler] Stop event set");
            break;
        default:
            snprintf(buf, sizeof(buf), "[ServiceControlHandler] Unknown control: %lu", CtrlCode);
            OutputDebugString(buf);
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
    OutputDebugString("[ServiceMain] Starting");
    g_StatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceControlHandler);
    if (!g_StatusHandle) {
        OutputDebugString("[ServiceMain] RegisterServiceCtrlHandler failed");
        return;
    }

    OutputDebugString("[ServiceMain] Handler registered");
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_ServiceStatus.dwWin32ExitCode = NO_ERROR;
    g_ServiceStatus.dwServiceSpecificExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 1;
    g_ServiceStatus.dwWaitHint = 5000;
    UpdateServiceStatus();

    OutputDebugString("[ServiceMain] Creating stop event");
    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_ServiceStopEvent) {
        OutputDebugString("[ServiceMain] CreateEvent failed");
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        UpdateServiceStatus();
        return;
    }

    load_config(g_ConfigPath);

    OutputDebugString("[ServiceMain] Starting IPC");
    IpcStart(g_ServiceStopEvent);

    g_ClientStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    LOGF("[ServiceMain] Starting client backend, server: %s", g_ServerIp[0] ? g_ServerIp : "(not set)");
    spawn_client_backend();

    HANDLE monitor_thread = CreateThread(NULL, 0, client_monitor_thread, NULL, 0, NULL);
    if (monitor_thread) {
        CloseHandle(monitor_thread);
        LOGF("[ServiceMain] Client monitor thread started");
    }

    OutputDebugString("[ServiceMain] Creating worker thread");
    HANDLE hThread = CreateThread(NULL, 0, FirewallServiceThread, NULL, 0, NULL);
    if (!hThread) {
        OutputDebugString("[ServiceMain] CreateThread failed");
        CloseHandle(g_ServiceStopEvent);
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        UpdateServiceStatus();
        return;
    }

    OutputDebugString("[ServiceMain] Setting RUNNING state");
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwCheckPoint = 0;
    g_ServiceStatus.dwWaitHint = 0;
    UpdateServiceStatus();
    OutputDebugString("[ServiceMain] Running");

    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);

    stop_client_backend();
    CloseHandle(g_ClientStopEvent);
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
    init_paths();
    log_init_async(LOG_BUFFER_SIZE);
    init_logging();
    
    LOGF("[Firewall] Started");
    
    LoadWhiteList(NULL);
    LOGF("[Firewall] Started with %zu whitelisted domains and %zu allowed IPs",
          g_Whitelist.count, g_IpAllowlist.count);

    SERVICE_TABLE_ENTRY serviceTable[] = {
        {SERVICE_NAME, ServiceMain},
        {NULL, NULL}
    };

    StartServiceCtrlDispatcher(serviceTable);
    log_shutdown();
    return 0;
}
