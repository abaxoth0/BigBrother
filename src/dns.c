/** @file dns.c
 * @brief DNS packet parser implementation.
 */

#include "../include/dns.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
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
    size_t original_offset = offset; // Save for uncompressed names
    int jumped = 0; // Track if we followed a pointer
    int loops = 0;

    while (offset < payload_len && payload[offset] != 0) {
        if (loops++ > 100) break;  // Prevent infinite loops from malformed packets

        uint8_t label_len = payload[offset];

        /*
         * Check if this is a compression pointer.
         * DNS compression uses two bytes:
         * - First byte: 11 (bits 7-6 = 1, bits 5-0 = offset high bits)
         * - Second byte: offset low bits
         *
         * Bitwise check: (label_len & 0xC0) == 0xC0
         *   0xC0 = 192 = 11000000 in binary
         *   This checks if the two most significant bits are both set
         */
        if ((label_len & 0xC0) == 0xC0) {
            // This is a compression pointer - follow it
            if (offset + 1 >= payload_len) break;

            /*
             * Extract the 14-bit offset from the two bytes:
             * - Lower 6 bits of first byte (mask 0x3F = 00111111)
             * - All 8 bits of second byte
             *
             * Example: bytes = 0xC0, 0x0C
             *   (0xC0 & 0x3F) = 0x00 (lower 6 bits)
             *   (0x00 << 8)    = 0x0000
             *   | 0x0C         = 0x000C = offset 12
             */
            uint16_t jump_offset = ((label_len & 0x3F) << 8) | payload[offset + 1];

            // Save position after pointer for when we return
            if (!jumped) {
                original_offset = offset + 2;
            }

            // Jump to the offset position in packet
            offset = jump_offset;
            jumped = 1;
            continue;
        }

        // Regular label - skip past length byte
        offset++;

        // Add dot separator between labels (except before first label)
        if (out_pos > 0 && out_pos < out_len - 1) {
            out[out_pos++] = '.';
        }

        // Calculate how many bytes to copy
        size_t copy_len = label_len;
        if (out_pos + copy_len >= out_len) {
            // Truncate if output buffer is too small
            copy_len = out_len - out_pos - 1;
        }

        /*
         * Copy label bytes from packet to output.
         * Pointer arithmetic: payload + offset gives us the address
         * of the label data, then we copy 'copy_len' bytes.
         */
        memcpy(out + out_pos, payload + offset, copy_len);
        out_pos += copy_len;
        offset += copy_len;
    }

    out[out_pos] = '\0';

    /*
     * Return appropriate offset:
     * - If we followed pointers (jumped=1), return position after the pointer
     * - If uncompressed, return position after the null terminator
     */
    if (!jumped) {
        return offset + 1;
    }

    return original_offset;
}


/*
 * to_lower_inplace - Convert string to lowercase (ASCII)
 *
 * Domain names should be compared case-insensitively per RFC 4343.
 * This function modifies the string in place.
 *
 * Note: We use tolower() from ctype.h, but cast to (unsigned char)
 * because tolower() has undefined behavior for negative values
 * (char can be signed on some platforms).
 */
static void to_lower_inplace(char* str) {
    for (int i = 0; str[i]; i++) {
        str[i] = (char)tolower((unsigned char)str[i]);
    }
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

    /*
     * Extract flags field and check opcode.
     * ntohs() converts from network byte order to host byte order.
     * On little-endian systems (x86/x64), bytes are swapped.
     */
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

    // Minimum: 12-byte header + at least 5 bytes for question (name + type + class)
    if (payload_len < sizeof(DnsHeader) + 5) {
        return false;
    }

    return true;
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

    // Copy and convert each field from network byte order
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
     * Technique: shift right by 15, then mask with 1.
     *   0 = query, 1 = response
     */
    packet.is_response = (packet.header.flags >> 15) & 1;

    /*
     * Start parsing after the 12-byte header.
     * offset is our position in the packet data.
     */
    size_t offset = sizeof(DnsHeader);

    // Parse the question section (if any questions exist)
    if (packet.header.question_count > 0) {
        /*
         * Call parse_dns_name to extract the domain name.
         * This function handles both plain and compressed names.
         * It returns the new offset position after the name.
         */
        offset += parse_dns_name(payload, payload_len, offset,
                                 packet.question.domain, DNS_MAX_DOMAIN_LEN);

        // After name, question has: type (2 bytes) + class (2 bytes)
        if (offset + 4 <= payload_len) {
            /*
             * Read type and class fields.
             * Pointer arithmetic: payload + offset gives address of type field.
             * Cast to uint16_t* and dereference, then convert byte order.
             */
            packet.question.type = ntohs(*(uint16_t*)(payload + offset));
            packet.question.qclass = ntohs(*(uint16_t*)(payload + offset + 2));
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
            offset += parse_dns_name(payload, payload_len, offset, name, 255);

            // Check we have enough bytes for fixed fields (10 bytes)
            if (offset + 10 > payload_len) break;

            uint16_t rtype = ntohs(*(uint16_t*)(payload + offset));
            uint16_t rclass = ntohs(*(uint16_t*)(payload + offset + 2));
            uint32_t ttl = ntohl(*(uint32_t*)(payload + offset + 4));
            uint16_t rdlen = ntohs(*(uint16_t*)(payload + offset + 8));

            (void)rclass;  // Unused but extracted for completeness
            (void)ttl;     // Could be used for TTL-based caching

            // Move past the 10-byte fixed header
            offset += 10;

            // Make sure we have the full record data
            if (offset + rdlen > payload_len) break;

            /*
             * Check record type:
             * - TYPE A (1): IPv4 address, 4 bytes
             * - TYPE AAAA (28): IPv6 address, 16 bytes
             */
            if (rtype == DNS_TYPE_A && rdlen == 4) {
                // IPv4 address - read 4 bytes directly
                uint32_t ip = *(uint32_t*)(payload + offset);
                packet.answers[answer_idx].ip_count = 1;
                packet.answers[answer_idx].ips[0] = ip;
                strncpy(packet.answers[answer_idx].domain, name, DNS_MAX_DOMAIN_LEN);
                packet.answers[answer_idx].domain[DNS_MAX_DOMAIN_LEN] = '\0';
                answer_idx++;
            } else if (rtype == DNS_TYPE_AAAA && rdlen == 16) {
                // IPv6 address - read 16 bytes
                memcpy(packet.answers[answer_idx].ip6s[0], payload + offset, 16);
                packet.answers[answer_idx].ip6_count = 1;
                strncpy(packet.answers[answer_idx].domain, name, DNS_MAX_DOMAIN_LEN);
                packet.answers[answer_idx].domain[DNS_MAX_DOMAIN_LEN] = '\0';
                answer_idx++;
            }

            // Move past the variable-length rdata
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
    if (domain == NULL || whitelist == NULL) {
        return -1;
    }

    // Convert domain to lowercase for comparison
    char domain_lower[256];
    strncpy(domain_lower, domain, 255);
    domain_lower[255] = '\0';
    to_lower_inplace(domain_lower);

    for (size_t i = 0; i < whitelist_count; i++) {
        if (whitelist[i] == NULL) continue;

        char wl_lower[256];
        strncpy(wl_lower, whitelist[i], 255);
        wl_lower[255] = '\0';
        to_lower_inplace(wl_lower);

        /*
         * Check for wildcard pattern: "*.example.com"
         * Format: starts with "*." (asterisk, dot)
         */
        if (wl_lower[0] == '*' && wl_lower[1] == '.') {
            // Extract suffix after "*."
            const char* suffix = wl_lower + 2;
            size_t suffix_len = strlen(suffix);
            size_t domain_len = strlen(domain_lower);

            // Check if domain ends with the suffix
            if (domain_len >= suffix_len) {
                // Point to position where suffix should start
                const char* pos = domain_lower + domain_len - suffix_len;

                /*
                 * Verify:
                 * 1. Suffix matches exactly
                 * 2. Either exact length match OR preceded by dot
                 *    (prevents "notexample.com" matching "example.com")
                 */
                if (strcmp(pos, suffix) == 0 &&
                    (domain_len == suffix_len || *(pos - 1) == '.')) {
                    return 1;
                }
            }
        } else if (strcmp(domain_lower, wl_lower) == 0) {
            return 1;
        }
    }

    return 0;
}
