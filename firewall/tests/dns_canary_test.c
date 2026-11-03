#include <winsock2.h>
#include <windows.h>
#include <windivert.h>

// Run the packet loop with real packet helpers and no WinDivert driver.
static HANDLE test_open(const char*, WINDIVERT_LAYER, INT16, UINT64);
static BOOL test_set_param(HANDLE, WINDIVERT_PARAM, UINT64);
static BOOL test_recv(HANDLE, VOID*, UINT, UINT*, UINT64,
                      WINDIVERT_ADDRESS*, UINT*, LPOVERLAPPED);
static BOOL test_send(HANDLE, const VOID*, UINT, UINT*, const WINDIVERT_ADDRESS*);
static BOOL test_close(HANDLE);

#define WinDivertOpen test_open
#define WinDivertSetParam test_set_param
#define WinDivertRecvEx test_recv
#define WinDivertSend test_send
#define WinDivertClose test_close
#define main firewall_main
#include "../src/firewall.c"
#undef main

static unsigned char input[512], output[512];
static UINT input_len, output_len;
static WINDIVERT_ADDRESS input_addr, output_addr;
static int send_count, fail_send;

static HANDLE test_open(const char* filter, WINDIVERT_LAYER layer, INT16 priority, UINT64 flags) {
    (void)filter; (void)layer; (void)priority; (void)flags;
    return (HANDLE)1;
}

static BOOL test_set_param(HANDLE handle, WINDIVERT_PARAM param, UINT64 value) {
    (void)handle; (void)param; (void)value;
    return TRUE;
}

static BOOL test_recv(HANDLE handle, VOID* packet, UINT capacity, UINT* received,
                      UINT64 flags, WINDIVERT_ADDRESS* addr, UINT* addr_len,
                      LPOVERLAPPED overlapped) {
    (void)handle; (void)flags; (void)addr_len; (void)overlapped;
    assert(capacity >= input_len);
    memcpy(packet, input, input_len);
    *received = input_len;
    *addr = input_addr;
    assert(SetEvent(g_ServiceStopEvent));
    return TRUE;
}

static BOOL test_send(HANDLE handle, const VOID* packet, UINT len, UINT* sent,
                      const WINDIVERT_ADDRESS* addr) {
    (void)handle; (void)sent;
    assert(len <= sizeof(output));
    send_count++;
    memcpy(output, packet, len);
    output_len = len;
    output_addr = *addr;
    if (fail_send) SetLastError(ERROR_ACCESS_DENIED);
    return !fail_send;
}

static BOOL test_close(HANDLE handle) {
    (void)handle;
    return TRUE;
}

static UINT make_query(uint16_t type, uint16_t flags, int edns) {
    memset(input, 0, sizeof(input));
    WINDIVERT_IPHDR* ip = (WINDIVERT_IPHDR*)input;
    ip->Version = 4;
    ip->HdrLength = 5;
    ip->TTL = 64;
    ip->Protocol = IPPROTO_UDP;
    ip->SrcAddr = htonl(0xC0A80164);
    ip->DstAddr = htonl(0x08080808);
    WINDIVERT_UDPHDR* udp = (WINDIVERT_UDPHDR*)(ip + 1);
    udp->SrcPort = htons(53001);
    udp->DstPort = htons(53);
    uint8_t* data = (uint8_t*)(udp + 1);
    DnsHeader* dns = (DnsHeader*)data;
    dns->transaction_id = htons(0xABCD);
    dns->flags = htons(flags);
    dns->question_count = htons(1);
    const char name[] = "\x13" "UsE-Application-DNS" "\x03" "NET";
    memcpy(data + sizeof(*dns), name, sizeof(name));
    UINT question_end = sizeof(*dns) + sizeof(name);
    data[question_end++] = (uint8_t)(type >> 8);
    data[question_end++] = (uint8_t)type;
    data[question_end++] = 0;
    data[question_end++] = 1;
    UINT dns_len = question_end;
    if (edns) {
        const uint8_t opt[] = {0, 0, 41, 0x10, 0, 0, 0, 0x80, 0, 0, 0};
        memcpy(data + dns_len, opt, sizeof(opt));
        dns_len += sizeof(opt);
        dns->additional_count = htons(1);
    }
    udp->Length = htons(sizeof(*udp) + dns_len);
    input_len = sizeof(*ip) + sizeof(*udp) + dns_len;
    ip->Length = htons(input_len);
    memset(&input_addr, 0, sizeof(input_addr));
    input_addr.Outbound = 1;
    input_addr.Network.IfIdx = 7;
    input_addr.Network.SubIfIdx = 2;
    assert(WinDivertHelperCalcChecksums(input, input_len, &input_addr, 0));
    g_FiltrationEnabled = 1;
    fail_send = 0;
    return sizeof(*ip) + sizeof(*udp) + question_end;
}

static void run_packet(void) {
    send_count = 0;
    output_len = 0;
    assert(WinDivertHelperCalcChecksums(input, input_len, &input_addr, 0));
    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    assert(g_ServiceStopEvent);
    assert(FirewallServiceThread(NULL) == STATUS_OK);
    CloseHandle(g_ServiceStopEvent);
    assert(send_count == 1);
}

static void assert_passthrough(void) {
    run_packet();
    assert(output_len == input_len);
    assert(memcmp(input, output, input_len) == 0);
    assert(memcmp(&input_addr, &output_addr, sizeof(input_addr)) == 0);
}

static void check_response(uint16_t type, uint16_t query_flags, int edns, int fail) {
    UINT expected_len = make_query(type, query_flags, edns);
    fail_send = fail;
    run_packet();
    assert(output_len == expected_len);
    assert(!output_addr.Outbound);
    assert(output_addr.Network.IfIdx == input_addr.Network.IfIdx);
    assert(output_addr.Network.SubIfIdx == input_addr.Network.SubIfIdx);
    assert(output_addr.IPChecksum && output_addr.UDPChecksum);

    WINDIVERT_IPHDR* ip = (WINDIVERT_IPHDR*)output;
    WINDIVERT_IPHDR* original_ip = (WINDIVERT_IPHDR*)input;
    WINDIVERT_UDPHDR* udp = (WINDIVERT_UDPHDR*)(ip + 1);
    WINDIVERT_UDPHDR* original_udp = (WINDIVERT_UDPHDR*)(original_ip + 1);
    uint8_t* data = (uint8_t*)(udp + 1);
    DnsHeader* dns = (DnsHeader*)data;
    assert(ip->SrcAddr == original_ip->DstAddr && ip->DstAddr == original_ip->SrcAddr);
    assert(udp->SrcPort == original_udp->DstPort && udp->DstPort == original_udp->SrcPort);
    assert(ntohs(ip->Length) == output_len);
    assert(ntohs(udp->Length) == output_len - sizeof(*ip));
    assert(ntohs(dns->transaction_id) == 0xABCD);
    assert(ntohs(dns->flags) == (0x8083 | (query_flags & 0x0100)));
    assert(ntohs(dns->question_count) == 1);
    assert(!dns->answer_count && !dns->authority_count && !dns->additional_count);
    UINT question_offset = sizeof(*ip) + sizeof(*udp) + sizeof(*dns);
    assert(memcmp(input + question_offset, output + question_offset,
                  output_len - question_offset) == 0);
    unsigned char recalculated[512];
    memcpy(recalculated, output, output_len);
    assert(WinDivertHelperCalcChecksums(recalculated, output_len, NULL, 0));
    assert(memcmp(recalculated, output, output_len) == 0);
    DnsPacket parsed = DnsParse(data, ntohs(udp->Length) - sizeof(*udp));
    assert(parsed.is_valid && parsed.is_response && !parsed.answer_count);
    assert(strcmp(parsed.question.domain, "use-application-dns.net") == 0);
    assert(parsed.question.type == type && parsed.question.qclass == 1);
}

int main(void) {
    check_response(1, 0x0100, 0, 0);
    check_response(28, 0x0100, 1, 0);
    check_response(65, 0x0120, 1, 0);
    check_response(1, 0, 0, 0);
    check_response(1, 0x0100, 0, 1);

    make_query(1, 0x0100, 1);
    g_FiltrationEnabled = 0;
    assert_passthrough();
    make_query(1, 0x0100, 0);
    input_addr.Outbound = 0;
    assert_passthrough();
    make_query(1, 0x8180, 0);
    assert_passthrough();
    make_query(1, 0x0100, 0);
    DnsHeader* dns = (DnsHeader*)(input + 28);
    dns->question_count = htons(2);
    assert_passthrough();
    make_query(1, 0x0100, 0);
    input[41] = 'X';
    assert_passthrough();
    make_query(1, 0x0100, 0);
    input[input_len - 1] = 3;
    assert_passthrough();

    uint8_t malformed[32] = {0};
    malformed[5] = 1;
    malformed[12] = 0xC0; malformed[13] = 12;
    assert(DnsGetQuestionEnd(malformed, sizeof(malformed)) == 0);
    malformed[12] = 64;
    assert(DnsGetQuestionEnd(malformed, sizeof(malformed)) == 0);
    malformed[12] = 20;
    assert(DnsGetQuestionEnd(malformed, sizeof(malformed)) == 0);
    make_query(1, 0x0100, 0);
    assert(DnsGetQuestionEnd(input + 28, input_len - 29) == 0);
    puts("DNS canary replies, routing, checksums and passthrough passed");
    return 0;
}
