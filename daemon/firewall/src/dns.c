/** @file dns.c
 * @brief DNS packet parser implementation.
 */

#include "../include/dns.h"
#include "../include/common.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// DNS spec
// https://datatracker.ietf.org/doc/html/rfc1035

#define DNS_TYPE_A     1   // IPv4 address (A record)
#define DNS_TYPE_NS    2   // Name server
#define DNS_TYPE_CNAME 5   // Canonical name (alias)
#define DNS_TYPE_SOA   6   // Start of authority
#define DNS_TYPE_PTR   12  // Pointer (reverse DNS)
#define DNS_TYPE_MX    15  // Mail exchange
#define DNS_TYPE_TXT   16  // Text record
#define DNS_TYPE_AAAA  28  // IPv6 address (quad-A)
#define DNS_TYPE_SRV   33  // Service location
#define DNS_TYPE_ANY   255 // Any type (wildcard)

#define DNS_CLASS_IN   1   // Internet class

static uint32_t parse_dns_name(const uint8_t* payload, size_t payload_len,
                               size_t offset, char* out, size_t out_len) {
    size_t out_pos = 0;
    size_t original_offset = offset;
    int jumped = 0;
    int loops = 0;

    // Handles 1 label per iteration (except if it is a compression pointers)
    while (offset < payload_len && payload[offset] != 0) {
        if (loops++ > 100) break;

        uint8_t label_len = payload[offset];

        // Bytes from 0xC0 to 0xFF are compression pointers.
        // Compression pointer structure (RFC 1035 p4.1.4)
        //
        //    0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
        //  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
        //  | 1  1|                OFFSET                   |
        //  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
        //
        if (label_len >= 0xC0) {
            if (offset + 1 >= payload_len) break;

            // 0x3F mask is 0b00111111, it is used to remove first two MSB (which are 1)
            // doing so we can get first 6 bits of the offset of the compression pointer.
            // To get full offset need to shift it by 8 bit to left (to make room for the second byte)
            // and OR it with the second byte.
            uint16_t jump_offset = ((label_len & 0x3F) << 8) | payload[offset + 1];

            if (!jumped) {
                original_offset = offset + 2;
            }

            offset = jump_offset;
            jumped = 1;
            continue;
        }

        // Labels are at most 63 octets (RFC 1035). Bytes 0x40..0xBF are reserved
        // and must not be treated as a label length.
        if (label_len > 63) break;

        offset++;

        if (out_pos > 0 && out_pos < out_len - 1) {
            out[out_pos++] = '.';
        }

        // Clamp against BOTH the output buffer and the payload boundary to avoid
        // reading past the packet (remote heap over-read).
        size_t copy_len = label_len;
        size_t avail_in = payload_len - offset; // offset < payload_len guaranteed
        if (copy_len > avail_in) copy_len = avail_in;
        if (out_pos + copy_len >= out_len) {
            copy_len = out_len - out_pos - 1;
        }

        memcpy(out + out_pos, payload + offset, copy_len);
        out_pos += copy_len;
        offset += copy_len;
    }

    out[out_pos] = '\0';

    if (!jumped) {
        return offset + 1;
    }

    return original_offset;
}

bool DnsIsDnsPacket(const uint8_t* payload, size_t payload_len) {
    if (payload == NULL || payload_len < sizeof(DnsHeader)) {
        return false;
    }

    DnsHeader* hdr = (DnsHeader*)payload;
    uint16_t flags = ntohs(hdr->flags);

    // Opcode: bits 11-14 of flags field.
    // 0xF is 0b00001111
    uint8_t opcode = (flags >> 11) & 0xF;

    if (opcode != DNS_QUERY_OPCODE) {
        return false;
    }

    uint16_t qdcount = ntohs(hdr->question_count);
    if (qdcount == 0) {
        return false;
    }

    return payload_len >= DNS_MIN_REQ_LEN;
}

size_t DnsGetQuestionEnd(const uint8_t* payload, size_t payload_len) {
    if (payload == NULL || payload_len < sizeof(DnsHeader)) {
        return 0;
    }

    DnsHeader* hdr = (DnsHeader*)payload;
    uint16_t qdcount = ntohs(hdr->question_count);
    if (qdcount == 0) return 0;

    char name[DNS_MAX_DOMAIN_LEN + 1];
    size_t offset = parse_dns_name(payload, payload_len, sizeof(DnsHeader), name, sizeof(name));

    // 2 bytes type + 2 bytes class
    if (offset + 4 > payload_len) return 0;
    return offset + 4;
}

DnsPacket DnsParse(const uint8_t* payload, size_t payload_len) {
    DnsPacket packet = {0};

    if (!DnsIsDnsPacket(payload, payload_len)) {
        return packet;
    }

    packet.is_valid = true;

    DnsHeader* hdr = (DnsHeader*)payload;

    packet.header.transaction_id = ntohs(hdr->transaction_id);
    packet.header.flags = ntohs(hdr->flags);
    packet.header.question_count = ntohs(hdr->question_count);
    packet.header.answer_count = ntohs(hdr->answer_count);
    packet.header.authority_count = ntohs(hdr->authority_count);
    packet.header.additional_count = ntohs(hdr->additional_count);

    // QR bit is bit 15 (MSB) of flags.
    packet.is_response = (packet.header.flags >> 15) & 1;

    size_t offset = sizeof(DnsHeader);

    if (packet.header.question_count > 0) {
        offset = parse_dns_name(payload, payload_len, offset,
                                 packet.question.domain, DNS_MAX_DOMAIN_LEN);

        // 2 bytes for type and 2 bytes for class
        if (offset + 4 <= payload_len) {
            packet.question.type = ntohs(*(uint16_t*)(payload + offset));
            packet.question.qclass = ntohs(*(uint16_t*)(payload + offset + 2));
            offset += 4;
        }
    }

    ToLowerInplace(packet.question.domain);

    if (!packet.is_response || packet.header.answer_count == 0) {
        return packet;
    }

    /*
     * Parse answer section for DNS responses.
     * Only responses (is_response = true) contain answer records.
     *
     * DNS Answer Record structure:
     *   Name:     variable (pointer or inline)
     *   Type:     2 bytes (A, AAAA, CNAME, etc.)
     *   Class:    2 bytes (usually IN = 1)
     *   TTL:      4 bytes (time-to-live in seconds)
     *   RDLength: 2 bytes (length of RData)
     *   RData:    variable (IP for A, IPv6 for AAAA, etc.)
     */
    size_t answer_idx = 0;

    for (uint16_t i = 0; i < packet.header.answer_count && answer_idx < DNS_MAX_IPS; i++) {
        if (offset >= payload_len) break;

        char name[256] = {0};

        offset = parse_dns_name(payload, payload_len, offset, name, sizeof(name));

        // Need 10 bytes for fixed header (type, class, TTL, RDLength)
        if (offset + 10 > payload_len) break;

        // For future: offset + 2 is rclass
        uint16_t rtype = ntohs(*(uint16_t*)(payload + offset));
        uint32_t ttl   = ntohl(*(uint32_t*)(payload + offset + 4));
        uint16_t rdlen = ntohs(*(uint16_t*)(payload + offset + 8));

        if (offset >= payload_len) break;

        offset += 10;
        // Verify that there are enough space for rdata
        if (offset + rdlen > payload_len) break;

        if (rtype == DNS_TYPE_A && rdlen == IP_V4_SIZE) { // IPv4
            uint32_t ip = ntohl(*(uint32_t*)(payload + offset));
            packet.answers[answer_idx].ip_count = 1;
            packet.answers[answer_idx].ips[0] = ip;
        } else if (rtype == DNS_TYPE_AAAA && rdlen == IP_V6_SIZE) { // IPv6
            memcpy(packet.answers[answer_idx].ip6s[0], payload + offset, IP_V6_SIZE);
            packet.answers[answer_idx].ip6_count = 1;
        }
        strncpy(packet.answers[answer_idx].domain, name, DNS_MAX_STR_DOMAIN_LEN);
        packet.answers[answer_idx].domain[DNS_MAX_STR_DOMAIN_LEN-1] = '\0';
        packet.answers[answer_idx].ttl = ttl;
        answer_idx++;

        offset += rdlen;
    }

    packet.answer_count = answer_idx;

    return packet;
}

void DnsFree(DnsPacket* packet) {
    if (packet == NULL) return;
    memset(packet, 0, sizeof(DnsPacket));
}

int DnsCheckDomain(const char* domain, const char* pattern) {
    /*
     * Supported whitelist patterns:
     * 1. Exact match: "github.com" matches only "github.com"
     * 2. Wildcard suffix: "*.github.com" matches "api.github.com", "raw.githubusercontent.com"
     * 3. Substring: "\"github\"" matches any domain containing "github"
     *
     * Comparison is case-insensitive.
     */
    if (domain == NULL) {
        return -1;
    }

    char domain_lower[DNS_MAX_STR_DOMAIN_LEN];
    char pattern_lower[DNS_MAX_STR_DOMAIN_LEN];
    STR_COPY_LOWER(domain_lower, domain, DNS_MAX_STR_DOMAIN_LEN);
    STR_COPY_LOWER(pattern_lower, pattern, DNS_MAX_STR_DOMAIN_LEN);
    return DnsCheckDomainLower(domain_lower, pattern_lower);
}

// Same matching as DnsCheckDomain, but both inputs are expected to be already
// lowercased (the pattern is pre-lowercased at whitelist load time, the domain
// is lowercased once by the caller). Avoids per-entry re-lowercasing on the
// packet/DNS hot paths. Does not modify either input.
int DnsCheckDomainLower(const char* domain_lower, const char* pattern_lower) {
    if (domain_lower == NULL) {
        return -1;
    }

    size_t pattern_len = strlen(pattern_lower);
    int is_substring = 0;
    char substring[DNS_MAX_STR_DOMAIN_LEN];
    const char* pattern = pattern_lower;

    if (pattern_lower[0] == '"' && pattern_len >= 2 &&
        pattern_lower[pattern_len - 1] == '"') {
        // A lone quote ("") would match every domain — treat as no match.
        if (pattern_len == 2) {
            return 0;
        }
        // Copy without the quotes so the pattern is properly null-terminated
        // (the shared whitelist buffer must not be modified).
        memcpy(substring, pattern_lower + 1, pattern_len - 2);
        substring[pattern_len - 2] = '\0';
        pattern = substring;
        is_substring = 1;
    }

    // Handle pattern matching syntax
    if (is_substring && strstr(domain_lower, pattern) != NULL) {
        return 1;
    }

    // Handle wildcard syntax
    if (pattern[0] == '*' && pattern[1] == '.') {
        const char* suffix = pattern + 2;
        size_t suffix_len = strlen(suffix);
        size_t domain_len = strlen(domain_lower);

        if (domain_len >= suffix_len) {
            const char* pos = domain_lower + domain_len - suffix_len;
            if (strcmp(pos, suffix) == 0 && (domain_len == suffix_len || *(pos - 1) == '.')) {
                return 1;
            }
        }
    }

    // Handle exact match
    if (strcmp(domain_lower, pattern) == 0) {
        return 1;
    }

    return 0;
}
