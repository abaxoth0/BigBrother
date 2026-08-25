#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include "../include/net_client.h"
#include "../include/ipc_daemon.h"
#include <log/log.h>

#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

// ---------------------------------------------------------------------------
// Shared adapter cache
// ---------------------------------------------------------------------------

static IP_ADAPTER_ADDRESSES* g_adapters = NULL;

static IP_ADAPTER_ADDRESSES* get_adapters(void) {
    if (g_adapters) return g_adapters;
    ULONG buf_len = 0;
    GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, NULL, NULL, &buf_len);
    if (buf_len == 0) return NULL;
    g_adapters = (IP_ADAPTER_ADDRESSES*)malloc(buf_len);
    if (!g_adapters) return NULL;
    ULONG ret = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, NULL, g_adapters, &buf_len);
    if (ret != NO_ERROR) { free(g_adapters); g_adapters = NULL; return NULL; }
    return g_adapters;
}

static int is_virtual_adapter(const IP_ADAPTER_ADDRESSES* a);

// ---------------------------------------------------------------------------
// Local IP helper
// ---------------------------------------------------------------------------

int GetLocalIp(const char* server_ip, char* buffer, size_t buffer_size) {
    (void)server_ip;
    if (!buffer || buffer_size == 0) return -1;
    buffer[0] = '\0';

    // Use adapter enumeration instead of the old UDP-connect-to-port-445 trick:
    // that fails when port 445 is filtered (common on hardened networks).
    IP_ADAPTER_ADDRESSES* adapters = get_adapters();
    if (!adapters) return -1;

    for (IP_ADAPTER_ADDRESSES* a = adapters; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;
        if (a->FirstUnicastAddress == NULL) continue;
        if (a->FirstGatewayAddress == NULL || a->FirstGatewayAddress->Address.lpSockaddr == NULL) continue;
        if (is_virtual_adapter(a)) continue;

        struct sockaddr_in* sin = (struct sockaddr_in*)a->FirstUnicastAddress->Address.lpSockaddr;
        if (!sin) continue;

        uint32_t ip = ntohl(sin->sin_addr.s_addr);
        if (ip == 0x7f000001) continue;

        char* s = inet_ntoa(sin->sin_addr);
        if (s) {
            strncpy(buffer, s, buffer_size - 1);
            buffer[buffer_size - 1] = '\0';
            return 0;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Port and discovery toggle
// ---------------------------------------------------------------------------

int get_server_port(void) {
    char buf[16] = {0};
    if (ini_get_string("server", "port", buf, sizeof(buf)) && buf[0]) {
        int p = atoi(buf);
        if (p > 0 && p < 65536) return p;
    }
    return 1984;
}

int is_discovery_enabled(void) {
    char buf[8] = {0};
    if (ini_get_string("discovery", "enabled", buf, sizeof(buf)) && buf[0]) {
        return buf[0] == '1' || buf[0] == 't' || buf[0] == 'y';
    }
    return 1;
}

// ---------------------------------------------------------------------------
// Network adapter enumeration
// ---------------------------------------------------------------------------

// Parse an IP string "1.2.3.4" into a uint32_t in HOST byte order. Returns 0 on error.
static uint32_t parse_ip(const char* str) {
    unsigned int a, b, c, d;
    if (sscanf(str, "%u.%u.%u.%u", &a, &b, &c, &d) == 4)
        return (a << 24) | (b << 16) | (c << 8) | d;
    return 0;
}

// Convert a subnet mask string like "255.255.255.0" to a prefix length.
static int mask_to_prefix(uint32_t mask) {
    int p = 0;
    while (mask & 0x80000000) { p++; mask <<= 1; }
    return p;
}

// Find adapter matching a given gateway IP. Returns non-zero on success with ip_str filled.
static int find_adapter_by_gateway(const char* gateway_str, char* ip_str, size_t ip_size, uint32_t* out_ip, uint32_t* out_mask) {
    uint32_t target_gw = parse_ip(gateway_str);
    if (!target_gw) return 0;

    IP_ADAPTER_ADDRESSES* gaa_adapters = get_adapters();
    if (!gaa_adapters) return 0;

    int found = 0;
    for (IP_ADAPTER_ADDRESSES* a = gaa_adapters; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;
        if (a->FirstUnicastAddress == NULL) continue;
        if (a->FirstGatewayAddress == NULL || a->FirstGatewayAddress->Address.lpSockaddr == NULL) {
            struct sockaddr_in* usin = (struct sockaddr_in*)a->FirstUnicastAddress->Address.lpSockaddr;
            if (usin) {
                uint32_t uip = ntohl(usin->sin_addr.s_addr);
                LOGF("[Discovery] Adapter %u.%u.%u.%u: no gateway",
                     (uip >> 24) & 0xFF, (uip >> 16) & 0xFF, (uip >> 8) & 0xFF, uip & 0xFF);
            }
            continue;
        }

        struct sockaddr_in* gw_sin = (struct sockaddr_in*)a->FirstGatewayAddress->Address.lpSockaddr;
        if (!gw_sin) continue;
        uint32_t gw_ip = ntohl(gw_sin->sin_addr.s_addr);
        LOGF("[Discovery] Adapter gw=%u.%u.%u.%u, target=%u.%u.%u.%u",
             (gw_ip >> 24) & 0xFF, (gw_ip >> 16) & 0xFF, (gw_ip >> 8) & 0xFF, gw_ip & 0xFF,
             (target_gw >> 24) & 0xFF, (target_gw >> 16) & 0xFF, (target_gw >> 8) & 0xFF, target_gw & 0xFF);
        if (gw_ip != target_gw) continue;

        SOCKET_ADDRESS* saddr = &a->FirstUnicastAddress->Address;
        struct sockaddr_in* sin = (struct sockaddr_in*)saddr->lpSockaddr;
        if (!sin) continue;
        uint32_t ip = ntohl(sin->sin_addr.s_addr);
        if (ip == 0x7f000001) continue;

        if (ip_str)
            snprintf(ip_str, ip_size, "%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
        if (out_ip) *out_ip = ip;
        if (out_mask) {
            ULONG prefix = a->FirstUnicastAddress->OnLinkPrefixLength;
            *out_mask = prefix ? (0xFFFFFFFF << (32 - prefix)) : 0x00FFFFFF;
        }
        found = 1;
        break;
    }

    return found;
}

// ---------------------------------------------------------------------------
// Virtual adapter detection & local IP check
// ---------------------------------------------------------------------------

static const WCHAR* wcsistr(const WCHAR* haystack, const WCHAR* needle) {
    if (!haystack || !needle) return NULL;
    size_t needle_len = wcslen(needle);
    if (needle_len == 0) return haystack;
    size_t haystack_len = wcslen(haystack);
    if (needle_len > haystack_len) return NULL;
    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        size_t j;
        for (j = 0; j < needle_len; j++) {
            WCHAR h = haystack[i + j];
            WCHAR n = needle[j];
            if (h >= L'A' && h <= L'Z') h += L'a' - L'A';
            if (n >= L'A' && n <= L'Z') n += L'a' - L'A';
            if (h != n) break;
        }
        if (j == needle_len) return haystack + i;
    }
    return NULL;
}

static int is_virtual_adapter(const IP_ADAPTER_ADDRESSES* a) {
    if (!a || !a->Description) return 0;
    static const WCHAR* keywords[] = {
        L"hyper-v",
        L"virtual",
        L"vmware",
        L"vbox",
    };
    for (int i = 0; i < (int)(sizeof(keywords) / sizeof(keywords[0])); i++) {
        if (wcsistr(a->Description, keywords[i])) return 1;
    }
    return 0;
}

static int is_local_ip(uint32_t ip) {
    IP_ADAPTER_ADDRESSES* gaa_adapters = get_adapters();
    if (!gaa_adapters) return 0;

    for (IP_ADAPTER_ADDRESSES* a = gaa_adapters; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;
        if (a->FirstUnicastAddress == NULL) continue;
        struct sockaddr_in* sin = (struct sockaddr_in*)a->FirstUnicastAddress->Address.lpSockaddr;
        if (!sin) continue;
        uint32_t adapter_ip = ntohl(sin->sin_addr.s_addr);
        if (adapter_ip == ip) return 1;
        if (adapter_ip == 0x7f000001) continue;
    }
    return 0;
}

int GetAllBroadcastAddresses(char* ip_str, size_t ip_size, char* bcast_out, size_t bcast_size) {
    if (ip_str) ip_str[0] = '\0';
    if (bcast_out) bcast_out[0] = '\0';

    int discovery_enabled = is_discovery_enabled();
    if (!discovery_enabled) {
        LOGF("[Discovery] Discovery is disabled in config, detecting local IP only");
    }

    // Check if manual network config is enabled
    char auto_buf[8] = {0};
    int auto_mode = 1;
    if (ini_get_string("network", "auto", auto_buf, sizeof(auto_buf)) && auto_buf[0])
        auto_mode = (auto_buf[0] == '1' || auto_buf[0] == 't' || auto_buf[0] == 'y');

    if (!auto_mode) {
        char gw_buf[64] = {0}, mask_buf[64] = {0};
        ini_get_string("network", "gateway", gw_buf, sizeof(gw_buf));
        ini_get_string("network", "mask", mask_buf, sizeof(mask_buf));

        if (gw_buf[0] && mask_buf[0]) {
            uint32_t ip = 0, adapter_mask = 0;
            if (find_adapter_by_gateway(gw_buf, ip_str, ip_size, &ip, &adapter_mask)) {
                uint32_t manual_mask = parse_ip(mask_buf);
                uint32_t prefix = manual_mask ? mask_to_prefix(manual_mask) : 24;
                uint32_t mask_val = (0xFFFFFFFF << (32 - prefix));
                uint32_t bcast = ip | ~mask_val;
                snprintf(bcast_out, bcast_size, "%u.%u.%u.%u\n",
                    (bcast >> 24) & 0xFF, (bcast >> 16) & 0xFF, (bcast >> 8) & 0xFF, bcast & 0xFF);
                LOGF("[Discovery] Manual network: gw=%s mask=%s -> broadcast %s", gw_buf, mask_buf, bcast_out);
                return 1;
            }
            LOGF("[Discovery] No adapter found for gateway %s", gw_buf);
        }
        return 0;
    }

    IP_ADAPTER_ADDRESSES* gaa_adapters = get_adapters();
    if (!gaa_adapters) return 0;

    int count = 0;
    char* out = bcast_out;
    size_t remaining = bcast_size;

    for (IP_ADAPTER_ADDRESSES* a = gaa_adapters; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;
        if (a->FirstUnicastAddress == NULL) continue;
        if (a->FirstGatewayAddress == NULL || a->FirstGatewayAddress->Address.lpSockaddr == NULL) continue;
        if (is_virtual_adapter(a)) continue;

        SOCKET_ADDRESS* saddr = &a->FirstUnicastAddress->Address;
        struct sockaddr_in* sin = (struct sockaddr_in*)saddr->lpSockaddr;
        if (!sin) continue;

        ULONG ip = ntohl(sin->sin_addr.s_addr);
        if (ip == 0x7f000001) continue;

        if (discovery_enabled) {
            ULONG prefix = a->FirstUnicastAddress->OnLinkPrefixLength;
            ULONG mask_val = prefix ? (0xFFFFFFFF << (32 - prefix)) : 0x00FFFFFF;
            ULONG bcast = ip | ~mask_val;

            if (out && remaining > 0) {
                int written = snprintf(out, remaining, "%lu.%lu.%lu.%lu\n",
                    (bcast >> 24) & 0xFF, (bcast >> 16) & 0xFF, (bcast >> 8) & 0xFF, bcast & 0xFF);
                if (written > 0 && written < (int)remaining) {
                    out += written;
                    remaining -= written;
                    count++;
                }
            }
        }
    }

    if (count == 0 && discovery_enabled && bcast_out && bcast_size > 0) {
        snprintf(bcast_out, bcast_size, "255.255.255.255\n");
        count = 1;
    }

    return count;
}

// ---------------------------------------------------------------------------
// UDP broadcast discovery
// ---------------------------------------------------------------------------

int DiscoverServers(const char* bcast_list, int port, int timeout_ms, char* out, size_t out_size) {
    if (!out || out_size == 0) return 0;
    out[0] = '\0';

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return 0;
    }

    int bcast_opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char*)&bcast_opt, sizeof(bcast_opt));
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    DWORD rcv_timeout = timeout_ms > 0 ? timeout_ms : DISCOVERY_TIMEOUT_MS;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&rcv_timeout, sizeof(rcv_timeout));

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = htons(42068);
    if (bind(sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) == SOCKET_ERROR) {
        LOGF("[Discovery] bind() failed: %lu", (unsigned long)WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 0;
    }

    char request[128];
    snprintf(request, sizeof(request), "%s\n1\n", DISCOVERY_MAGIC);

    LOGF("[Discovery] Broadcast list: %s", bcast_list ? bcast_list : "(null)");

    {
        char list_copy[4096] = {0};
        strncpy(list_copy, bcast_list ? bcast_list : "127.0.0.1", sizeof(list_copy) - 1);

        struct sockaddr_in dest;
        memset(&dest, 0, sizeof(dest));
        dest.sin_family = AF_INET;
        dest.sin_port = htons((unsigned short)port);

        char* line = list_copy;
        char* list_end = list_copy + sizeof(list_copy) - 1;
        while (line >= list_copy && line < list_end && *line) {
            char* nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            if (line[0] != '\0') {
                inet_pton(AF_INET, line, &dest.sin_addr);
                int sent = sendto(sock, request, (int)strlen(request), 0, (struct sockaddr*)&dest, sizeof(dest));
                if (sent == SOCKET_ERROR) {
                    LOGF("[Discovery] sendto to %s failed: %lu", line, (unsigned long)WSAGetLastError());
                }
            }
            if (nl && nl < list_end) line = nl + 1; else break;
        }
    }

    LOGF("[Discovery] Waiting %d ms for responses...", timeout_ms);

    int count = 0;
    char* out_pos = out;
    size_t remaining = out_size - 1;

    while (count < DISCOVERY_MAX_SERVERS) {
        struct sockaddr_in from;
        int from_len = sizeof(from);
        char buf[DISCOVERY_BUF_SIZE] = {0};
        int n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&from, &from_len);
        if (n == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) break;
            if (err == WSAECONNRESET) continue;
            LOGF("[Discovery] recvfrom error: %d", err);
            break;
        }
        if (n <= 0) break;

        buf[n] = '\0';

        char* line = buf;
        if (strncmp(line, DISCOVERY_RESPONSE_MAGIC, strlen(DISCOVERY_RESPONSE_MAGIC)) != 0) continue;

        line = strchr(line, '\n');
        if (!line) continue;
        line++;

        char* name = line;
        char* nl = strchr(line, '\n');
        if (!nl) continue;
        *nl = '\0';
        line = nl + 1;

        char* ip = line;
        nl = strchr(line, '\n');
        if (!nl) continue;
        *nl = '\0';
        line = nl + 1;

        {
            uint32_t ip_addr = parse_ip(ip);
            if (ip_addr && is_local_ip(ip_addr)) {
                LOGF("[Discovery] Server IP %s is local, replacing with 127.0.0.1", ip);
                ip = "127.0.0.1";
            }
        }

        char* port = line;
        nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        // Dedup by IP (temp compare, no in-place buffer modification)
        int dup = 0;
        char* check = out;
        char* out_end = out_pos;
        while (check && check < out_end) {
            char* first_pipe = strchr(check, '|');
            if (first_pipe) {
                char* ip_start = first_pipe + 1;
                char* ip_end = strchr(ip_start, '|');
                char* nl = strchr(ip_start, '\n');
                char* term = NULL;
                if (ip_end && (!nl || ip_end < nl))
                    term = ip_end;
                else if (nl)
                    term = nl;
                if (term) {
                    size_t ip_len = term - ip_start;
                    if (ip_len < 64 && ip_len == strlen(ip) && memcmp(ip_start, ip, ip_len) == 0) {
                        dup = 1;
                        break;
                    }
                }
            }
            char* next_nl = strchr(check, '\n');
            check = next_nl ? next_nl + 1 : NULL;
        }
        if (dup) continue;

        int written = snprintf(out_pos, remaining, "%s|%s|%s\n", name, ip, port && port[0] ? port : "1984");
        if (written > 0 && written < (int)remaining) {
            out_pos += written;
            remaining -= written;
            count++;
        } else {
            break;
        }
    }

    LOGF("[Discovery] UDP done: %d servers found", count);
    if (count > 0) LOGF("[Discovery] First response: %s", out);

    closesocket(sock);
    WSACleanup();
    return count;
}

// ---------------------------------------------------------------------------
// TCP subnet scan discovery
// ---------------------------------------------------------------------------

int DiscoverServersTCP(char* out, size_t out_size, int timeout_ms) {
    if (!out || out_size == 0) return 0;
    out[0] = '\0';

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;

    IP_ADAPTER_ADDRESSES* gaa_adapters = get_adapters();
    if (!gaa_adapters) { WSACleanup(); return 0; }

    int count = 0;
    char* out_pos = out;
    size_t remaining = out_size;
    if (timeout_ms < 5000) timeout_ms = 5000;
    ULONGLONG scan_end = GetTickCount64() + timeout_ms;

    // Check manual network config
    char auto_buf[8] = {0};
    int auto_mode = 1;
    if (ini_get_string("network", "auto", auto_buf, sizeof(auto_buf)) && auto_buf[0])
        auto_mode = (auto_buf[0] == '1' || auto_buf[0] == 't' || auto_buf[0] == 'y');

    if (!auto_mode) {
        char gw_buf[64] = {0}, mask_buf[64] = {0};
        ini_get_string("network", "gateway", gw_buf, sizeof(gw_buf));
        ini_get_string("network", "mask", mask_buf, sizeof(mask_buf));
        if (gw_buf[0] && mask_buf[0]) {
            uint32_t ip = 0, adapter_mask = 0;
            if (find_adapter_by_gateway(gw_buf, NULL, 0, &ip, &adapter_mask)) {
                uint32_t manual_mask = parse_ip(mask_buf);
                uint32_t prefix_val = manual_mask ? mask_to_prefix(manual_mask) : 24;
                uint32_t mask_val = (0xFFFFFFFF << (32 - prefix_val));
                uint32_t network = ip & mask_val;
                uint32_t bcast = ip | ~mask_val;
                uint32_t start = network + 1;
                uint32_t end = bcast - 1;
                LOGF("[Discovery] Manual TCP scan: gw=%s mask=%s range %u.%u.%u.%u-%u.%u.%u.%u",
                     gw_buf, mask_buf,
                     (network >> 24) & 0xFF, (network >> 16) & 0xFF, (network >> 8) & 0xFF, network & 0xFF,
                     (bcast >> 24) & 0xFF, (bcast >> 16) & 0xFF, (bcast >> 8) & 0xFF, bcast & 0xFF);
                // Scan the subnet
                for (uint32_t cur = start; cur <= end && count < DISCOVERY_MAX_SERVERS; ) {
                    if (GetTickCount64() >= scan_end) goto done;
                    int batch_size = 0;
                    SOCKET socks[DISCOVERY_MAX_CONCURRENT];
                    for (; batch_size < DISCOVERY_MAX_CONCURRENT && cur <= end && count + batch_size < DISCOVERY_MAX_SERVERS; cur++) {
                        socks[batch_size] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                        if (socks[batch_size] == INVALID_SOCKET) continue;
                        u_long nonblock = 1;
                        ioctlsocket(socks[batch_size], FIONBIO, &nonblock);
                        struct sockaddr_in target_addr;
                        memset(&target_addr, 0, sizeof(target_addr));
                        target_addr.sin_family = AF_INET;
                        target_addr.sin_port = htons(1984);
                        target_addr.sin_addr.s_addr = htonl(cur);
                        connect(socks[batch_size], (struct sockaddr*)&target_addr, sizeof(target_addr));
                        batch_size++;
                    }
                    if (batch_size == 0) break;
                    fd_set write_set;
                    FD_ZERO(&write_set);
                    SOCKET max_sock = 0;
                    for (int i = 0; i < batch_size; i++) { FD_SET(socks[i], &write_set); if (socks[i] > max_sock) max_sock = socks[i]; }
                    struct timeval tv = {0, DISCOVERY_TCP_TIMEOUT_MS * 1000};
                    select(max_sock + 1, NULL, &write_set, NULL, &tv);
                    for (int i = 0; i < batch_size; i++) {
                        if (!FD_ISSET(socks[i], &write_set)) { shutdown(socks[i], SD_BOTH); closesocket(socks[i]); continue; }
                        int err = 0; socklen_t err_len = sizeof(err);
                        getsockopt(socks[i], SOL_SOCKET, SO_ERROR, (char*)&err, &err_len);
                        if (err != 0) { shutdown(socks[i], SD_BOTH); closesocket(socks[i]); continue; }
                        u_long block = 0; ioctlsocket(socks[i], FIONBIO, &block);
                        DWORD st = 500; setsockopt(socks[i], SOL_SOCKET, SO_RCVTIMEO, (const char*)&st, sizeof(st));
                        setsockopt(socks[i], SOL_SOCKET, SO_SNDTIMEO, (const char*)&st, sizeof(st));
                        const char* req = "GET_SERVER_NAME\n\n";
                        int sent = send(socks[i], req, (int)strlen(req), 0);
                        if (sent <= 0) { shutdown(socks[i], SD_BOTH); closesocket(socks[i]); continue; }
                        char resp[1024] = {0}; int tt = 0; int err_count = 0;
                        while (tt < (int)sizeof(resp) - 1 && err_count < 3) {
                            int nn = recv(socks[i], resp + tt, sizeof(resp) - 1 - tt, 0);
                            if (nn == SOCKET_ERROR) {
                                int e = WSAGetLastError();
                                if (e == WSAECONNRESET) { shutdown(socks[i], SD_BOTH); closesocket(socks[i]); goto mscan_next; }
                                err_count++; continue;
                            }
                            if (nn == 0) break;
                            tt += nn;
                        }
                        resp[tt] = '\0'; shutdown(socks[i], SD_BOTH); closesocket(socks[i]);
                        mscan_next:
                        { char* nnl = strchr(resp, '\n'); if (!nnl) continue; *nnl = '\0';
                        if (strcmp(resp, "OK") != 0) continue;
                        char* ll = nnl + 1; nnl = strchr(ll, '\n'); if (!nnl) continue; *nnl = '\0';
                        ll = nnl + 1; nnl = strchr(ll, '\n'); if (nnl) *nnl = '\0';
                        char ips[16]; snprintf(ips, sizeof(ips), "%u.%u.%u.%u", (cur >> 24) & 0xFF, (cur >> 16) & 0xFF, (cur >> 8) & 0xFF, cur & 0xFF);
                        int wr = snprintf(out_pos, remaining, "%s|%s|1984\n", ll, ips);
                        if (wr > 0 && wr < (int)remaining) { out_pos += wr; remaining -= wr; count++; }
                        else if (wr >= (int)remaining) goto done; }
                    }
                }
            }
            LOGF("[Discovery] No adapter found for gateway %s", gw_buf);
        }
        goto probe_localhost;
    }

    // Auto mode: scan all physical adapters
    for (IP_ADAPTER_ADDRESSES* a = gaa_adapters; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;
        if (a->FirstUnicastAddress == NULL) continue;
        if (a->FirstGatewayAddress == NULL || a->FirstGatewayAddress->Address.lpSockaddr == NULL) continue;
        if (is_virtual_adapter(a)) continue;

        SOCKET_ADDRESS* saddr = &a->FirstUnicastAddress->Address;
        struct sockaddr_in* sin = (struct sockaddr_in*)saddr->lpSockaddr;
        if (!sin) continue;

        uint32_t ip = ntohl(sin->sin_addr.s_addr);
        if (ip == 0x7f000001) continue;

        ULONG prefix = a->FirstUnicastAddress->OnLinkPrefixLength;
        ULONG mask_val = prefix ? (0xFFFFFFFF << (32 - prefix)) : 0x00FFFFFF;
        uint32_t network = ip & mask_val;
        uint32_t bcast = ip | ~mask_val;

        uint32_t start = network + 1;
        uint32_t end = bcast - 1;

        LOGF("[Discovery] TCP scan subnet: %u.%u.%u.%u/%u (%u hosts)",
             (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF,
             (unsigned int)prefix, (unsigned int)(end - start + 1));

        for (uint32_t cur = start; cur <= end && count < DISCOVERY_MAX_SERVERS; ) {
            if (GetTickCount64() >= scan_end) goto done;

            int batch_size = 0;
            SOCKET socks[DISCOVERY_MAX_CONCURRENT];
            uint32_t targets[DISCOVERY_MAX_CONCURRENT];

            for (; batch_size < DISCOVERY_MAX_CONCURRENT && cur <= end && count + batch_size < DISCOVERY_MAX_SERVERS; cur++) {
                socks[batch_size] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (socks[batch_size] == INVALID_SOCKET) continue;

                u_long nonblock = 1;
                ioctlsocket(socks[batch_size], FIONBIO, &nonblock);

                struct sockaddr_in target_addr;
                memset(&target_addr, 0, sizeof(target_addr));
                target_addr.sin_family = AF_INET;
                target_addr.sin_port = htons(1984);
                target_addr.sin_addr.s_addr = htonl(cur);

                connect(socks[batch_size], (struct sockaddr*)&target_addr, sizeof(target_addr));
                targets[batch_size] = cur;
                batch_size++;
            }

            if (batch_size == 0) break;

            fd_set write_set;
            FD_ZERO(&write_set);
            SOCKET max_sock = 0;
            for (int i = 0; i < batch_size; i++) {
                FD_SET(socks[i], &write_set);
                if (socks[i] > max_sock) max_sock = socks[i];
            }
            struct timeval tv = {0, DISCOVERY_TCP_TIMEOUT_MS * 1000};
            select(max_sock + 1, NULL, &write_set, NULL, &tv);

            for (int i = 0; i < batch_size; i++) {
                if (!FD_ISSET(socks[i], &write_set)) {
                    shutdown(socks[i], SD_BOTH);
                    closesocket(socks[i]);
                    continue;
                }

                int err = 0;
                socklen_t err_len = sizeof(err);
                getsockopt(socks[i], SOL_SOCKET, SO_ERROR, (char*)&err, &err_len);
                if (err != 0) { shutdown(socks[i], SD_BOTH); closesocket(socks[i]); continue; }

                u_long block = 0;
                ioctlsocket(socks[i], FIONBIO, &block);
                DWORD st = 500;
                setsockopt(socks[i], SOL_SOCKET, SO_RCVTIMEO, (const char*)&st, sizeof(st));
                setsockopt(socks[i], SOL_SOCKET, SO_SNDTIMEO, (const char*)&st, sizeof(st));

                const char* req = "GET_SERVER_NAME\n\n";
                int sent = send(socks[i], req, (int)strlen(req), 0);
                if (sent <= 0) { shutdown(socks[i], SD_BOTH); closesocket(socks[i]); continue; }

                char resp[1024] = {0};
                int t = 0;
                int err_count = 0;
                while (t < (int)sizeof(resp) - 1 && err_count < 3) {
                    int n = recv(socks[i], resp + t, sizeof(resp) - 1 - t, 0);
                    if (n == SOCKET_ERROR) {
                        int err = WSAGetLastError();
                        if (err == WSAECONNRESET) { shutdown(socks[i], SD_BOTH); closesocket(socks[i]); goto done_sock; }
                        err_count++;
                        continue;
                    }
                    if (n == 0) break;
                    t += n;
                }
                resp[t] = '\0';
                shutdown(socks[i], SD_BOTH);
                closesocket(socks[i]);
                done_sock:

                char* nl = strchr(resp, '\n');
                if (!nl) continue;
                *nl = '\0';
                if (strcmp(resp, "OK") != 0) continue;
                char* l = nl + 1;
                nl = strchr(l, '\n');
                if (!nl) continue;
                *nl = '\0';
                l = nl + 1;
                nl = strchr(l, '\n');
                if (nl) *nl = '\0';

                char ip_str[16];
                snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u",
                    (targets[i] >> 24) & 0xFF, (targets[i] >> 16) & 0xFF,
                    (targets[i] >> 8) & 0xFF, targets[i] & 0xFF);

                int written = snprintf(out_pos, remaining, "%s|%s|1984\n", l, ip_str);
                if (written > 0 && written < (int)remaining) {
                    out_pos += written;
                    remaining -= written;
                    count++;
                } else if (written >= (int)remaining) {
                    goto done;
                }
            }
        }
    }

probe_localhost:
    // Probe 127.0.0.1 directly for same-machine discovery
    if (count < DISCOVERY_MAX_SERVERS) {
        SOCKET lsock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (lsock != INVALID_SOCKET) {
            u_long nonblock = 1;
            ioctlsocket(lsock, FIONBIO, &nonblock);
            struct sockaddr_in laddr;
            memset(&laddr, 0, sizeof(laddr));
            laddr.sin_family = AF_INET;
            laddr.sin_port = htons(1984);
            inet_pton(AF_INET, "127.0.0.1", &laddr.sin_addr);
            connect(lsock, (struct sockaddr*)&laddr, sizeof(laddr));
            fd_set ws;
            FD_ZERO(&ws);
            FD_SET(lsock, &ws);
            struct timeval tv = {0, 500 * 1000};
            if (select(0, NULL, &ws, NULL, &tv) > 0) {
                int err = 0;
                socklen_t el = sizeof(err);
                getsockopt(lsock, SOL_SOCKET, SO_ERROR, (char*)&err, &el);
                if (err == 0) {
                    u_long block = 0;
                    ioctlsocket(lsock, FIONBIO, &block);
                    DWORD st = 500;
                    setsockopt(lsock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&st, sizeof(st));
                    setsockopt(lsock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&st, sizeof(st));
                    const char* req = "GET_SERVER_NAME\n\n";
                    int sent = send(lsock, req, (int)strlen(req), 0);
                    if (sent <= 0) { shutdown(lsock, SD_BOTH); closesocket(lsock); goto done; }
                    char resp[1024] = {0};
                    int t = 0;
                    while (t < (int)sizeof(resp) - 1) {
                        int n = recv(lsock, resp + t, sizeof(resp) - 1 - t, 0);
                        if (n <= 0) break;
                        t += n;
                    }
                    resp[t] = '\0';
                    shutdown(lsock, SD_BOTH);
                    closesocket(lsock);

                    char* nl = strchr(resp, '\n');
                    if (nl) {
                        *nl = '\0';
                        if (strcmp(resp, "OK") == 0) {
                            char* l = nl + 1;
                            nl = strchr(l, '\n');
                            if (nl) {
                                l = nl + 1;
                                nl = strchr(l, '\n');
                                if (nl) *nl = '\0';
                                // Skip if same server was already found via subnet scan
                                char* existing = out;
                                int already_found = 0;
                                while (existing && existing < out_pos) {
                                    char* pipe = strchr(existing, '|');
                                    if (pipe) {
                                        *pipe = '\0';
                                        if (strcmp(existing, l) == 0) { already_found = 1; *pipe = '|'; break; }
                                        *pipe = '|';
                                    }
                                    char* next_nl = strchr(existing, '\n');
                                    existing = next_nl ? next_nl + 1 : NULL;
                                }
                                if (already_found) {
                                    LOGF("[Discovery] Skip 127.0.0.1 (already found via subnet): %s", l);
                                    goto skip_localhost_add;
                                }
                                int written = snprintf(out_pos, remaining, "%s|127.0.0.1|1984\n", l);
                                if (written > 0 && written < (int)remaining) {
                                    out_pos += written;
                                    remaining -= written;
                                    count++;
                                    LOGF("[Discovery] Found server at 127.0.0.1: %s", l);
                                }
                                skip_localhost_add: ;
                            }
                        }
                    }
                } else {
                    shutdown(lsock, SD_BOTH);
                    closesocket(lsock);
                }
            } else {
                shutdown(lsock, SD_BOTH);
                closesocket(lsock);
            }
        }
    }

done:
    WSACleanup();
    LOGF("[Discovery] TCP scan complete: %d servers found", count);
    return count;
}
