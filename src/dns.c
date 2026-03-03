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


/**
 * @brief Parse a DNS name from packet data.
 *
 * Handles both plain and compressed DNS name encoding.
 *
 * @param[in] payload     Pointer to start of DNS data.
 * @param[in] payload_len Total size of DNS data.
 * @param[in] offset      Current position in packet.
 * @param[out] out        Buffer for resulting domain name.
 * @param[in] out_len     Size of output buffer.
 *
 * @return New offset position after reading the name.
 */
static uint32_t parse_dns_name(const uint8_t* payload, size_t payload_len,
                               size_t offset, char* out, size_t out_len) {
    size_t out_pos = 0;
    size_t original_offset = offset;
    int jumped = 0;
    int loops = 0;

    while (offset < payload_len && payload[offset] != 0) {
        if (loops++ > 100) break;

        uint8_t label_len = payload[offset];

        if (label_len >= 0xC0) {
            if (offset + 1 >= payload_len) break;

            uint16_t jump_offset = ((label_len & 0x3F) << 8) | payload[offset + 1];

            if (!jumped) {
                original_offset = offset + 2;
            }

            offset = jump_offset;
            jumped = 1;
            continue;
        }

        offset++;

        if (out_pos > 0 && out_pos < out_len - 1) {
            out[out_pos++] = '.';
        }

        size_t copy_len = label_len;
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


/**
 * @brief Validate that payload is a DNS packet.
 *
 * Checks minimum size, standard query opcode, and question count.
 *
 * @param[in] payload     Pointer to DNS data.
 * @param[in] payload_len Length of DNS data.
 *
 * @return true if valid DNS packet, false otherwise.
 */
bool Dns_IsDnsPacket(const uint8_t* payload, size_t payload_len) {
    if (payload == NULL || payload_len < sizeof(DnsHeader)) {
        return false;
    }

    DnsHeader* hdr = (DnsHeader*)payload;
    uint16_t flags = ntohs(hdr->flags);

    /*
     * Opcode: bits 11-14 of flags field.
     * Shift right by 11 to move opcode to position 0-3,
     * then mask with 0xF (binary: 00001111) to keep only those bits.
     */
    uint8_t opcode = (flags >> 11) & 0xF;

    // Only standard queries (opcode 0) are valid for our purposes
    if (opcode != 0) {
        return false;
    }

    // Get question count - must be at least 1
    uint16_t qdcount = ntohs(hdr->question_count);
    if (qdcount == 0) {
        return false;
    }

    return payload_len < DNS_MIN_REQ_LEN;
}

/**
 * @brief Parse a DNS packet and extract domain and IP information.
 *
 * Parses the question section to get the queried domain name.
 * For responses, also parses the answer section for IPv4/IPv6 addresses.
 *
 * @param[in] payload     Pointer to DNS data.
 * @param[in] payload_len Length of DNS data.
 *
 * @return Parsed DnsPacket structure. Check is_valid field for success.
 */
DnsPacket Dns_Parse(const uint8_t* payload, size_t payload_len) {
    DnsPacket packet = {0};

    if (!Dns_IsDnsPacket(payload, payload_len)) {
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

    /*
     * Determine if this is a query or response.
     * QR bit is bit 15 (most significant bit) of flags.
     *
     * 0 = query, 1 = response
     */
    packet.is_response = (packet.header.flags >> 15) & 1;

    /*
     * Start parsing after the 12-byte header.
     * offset is our position in the packet data.
     */
    size_t offset = sizeof(DnsHeader);

    if (packet.header.question_count > 0) {
        offset = parse_dns_name(payload, payload_len, offset,
                                 packet.question.domain, DNS_MAX_DOMAIN_LEN);

        if (offset + 4 <= payload_len) {
            packet.question.type = ntohs(*(uint16_t*)(payload + offset));
            packet.question.qclass = ntohs(*(uint16_t*)(payload + offset + 2));
            offset += 4;
        }
    }

    to_lower_inplace(packet.question.domain);

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
    if (packet.is_response && packet.header.answer_count > 0) {
        size_t answer_idx = 0;

        for (uint16_t i = 0; i < packet.header.answer_count && answer_idx < DNS_MAX_IPS; i++) {
            if (offset >= payload_len) break;

            char name[256] = {0};

            uint8_t first_byte = payload[offset];

            if (first_byte == 0) {
                offset++;
            } else if (first_byte >= 0xC0 && offset + 1 < payload_len) {
                offset += 2;
            } else if (first_byte <= 63) {
                while (offset < payload_len) {
                    uint8_t b = payload[offset];
                    if (b == 0) {
                        offset++;
                        break;
                    } else if (b >= 0xC0) {
                        offset += 2;
                        break;
                    } else {
                        offset += b + 1;
                    }
                }
            } else {
                offset++;
            }

            if (offset + 10 > payload_len) break;

            uint16_t rtype = ntohs(*(uint16_t*)(payload + offset));
            uint16_t rclass = ntohs(*(uint16_t*)(payload + offset + 2));
            uint32_t ttl = ntohl(*(uint32_t*)(payload + offset + 4));
            uint16_t rdlen = ntohs(*(uint16_t*)(payload + offset + 8));

            (void)rclass;
            (void)ttl;

            if (offset >= payload_len) break;

            offset += 10;

            if (offset + rdlen > payload_len) break;

            if (rtype == DNS_TYPE_A && rdlen == 4) {
                uint32_t ip = *(uint32_t*)(payload + offset);
                packet.answers[answer_idx].ip_count = 1;
                packet.answers[answer_idx].ips[0] = ip;
                strncpy(packet.answers[answer_idx].domain, name, DNS_MAX_DOMAIN_LEN);
                packet.answers[answer_idx].domain[DNS_MAX_DOMAIN_LEN] = '\0';
                answer_idx++;
            } else if (rtype == DNS_TYPE_AAAA && rdlen == 16) {
                memcpy(packet.answers[answer_idx].ip6s[0], payload + offset, 16);
                packet.answers[answer_idx].ip6_count = 1;
                strncpy(packet.answers[answer_idx].domain, name, DNS_MAX_DOMAIN_LEN);
                packet.answers[answer_idx].domain[DNS_MAX_DOMAIN_LEN] = '\0';
                answer_idx++;
            }

            offset += rdlen;
        }

        packet.answer_count = answer_idx;
    }

    return packet;
}


/**
 * @brief Clean up a parsed DNS packet.
 *
 * Zeroes the packet structure. Currently a no-op since no dynamic
 * memory is allocated, but kept for API consistency and future use.
 *
 * @param[in,out] packet Pointer to DnsPacket to clean up.
 */
void Dns_Free(DnsPacket* packet) {
    if (packet == NULL) return;
    memset(packet, 0, sizeof(DnsPacket));
}


/**
 * @brief Check if domain matches any whitelist entry.
 *
 * Supports exact match (e.g., "github.com") and wildcard suffix
 * matching (e.g., "*.github.com" matches "api.github.com").
 * Comparison is case-insensitive.
 *
 * @param[in] domain          Domain name to check (e.g., "api.github.com").
 * @param[in] whitelist       Array of whitelist entry strings.
 * @param[in] whitelist_count Number of entries in whitelist array.
 *
 * @return 1 if whitelisted, 0 if not, -1 on error (null parameters).
 */
int Dns_CheckDomain(const char* domain, const char* whitelist[], size_t whitelist_count) {
    /*
     * Supported whitelist patterns:
     * 1. Exact match: "github.com" matches only "github.com"
     * 2. Wildcard suffix: "*.github.com" matches "api.github.com", "raw.githubusercontent.com"
     * 3. Substring: "\"github\"" matches any domain containing "github"
     *
     * Comparison is case-insensitive.
     */
    if (domain == NULL || whitelist == NULL) {
        return -1;
    }


    // Convert domain to lowercase for comparison
    char domain_lower[DNS_MAX_STR_DOMAIN_LEN];
    STR_COPY_LOWER(domain_lower, domain, DNS_MAX_STR_DOMAIN_LEN);

    for (size_t i = 0; i < whitelist_count; i++) {
        if (whitelist[i] == NULL) {
            assert(0 && "Corrupted whitelist: got NULL pointer instead of string");
            continue;
        }

        char wl_lower[DNS_MAX_STR_DOMAIN_LEN];
        STR_COPY_LOWER(wl_lower, whitelist[i], DNS_MAX_STR_DOMAIN_LEN);

        int is_substring = 0;
        if (wl_lower[0] == '"' && wl_lower[strlen(wl_lower)-1] == '"') {
            wl_lower[strlen(wl_lower)-1] = '\0';
            memmove(wl_lower, wl_lower + 1, strlen(wl_lower));
            is_substring = 1;
        }

        // Handle pattern matching syntax
        if (is_substring) {
            if (strstr(domain_lower, wl_lower) != NULL) {
                return 1;
            }
            return 0;
        }
        // Handle wildcard syntax
        if (wl_lower[0] == '*' && wl_lower[1] == '.') {
            const char* suffix = wl_lower + 2;
            size_t suffix_len = strlen(suffix);
            size_t domain_len = strlen(domain_lower);

            if (domain_len >= suffix_len) {
                const char* pos = domain_lower + domain_len - suffix_len;

                if (strcmp(pos, suffix) == 0 &&
                    (domain_len == suffix_len || *(pos - 1) == '.')) {
                    return 1;
                }
            }
            return 0;
        }
        // Handle exact match
        if (strcmp(domain_lower, wl_lower) == 0) {
            return 1;
        }
    }

    return 0;
}
