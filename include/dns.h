/** @file dns.h
 * @brief DNS packet parsing and validation for BigBrother firewall.
 */

#ifndef DNS_H
#define DNS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define DNS_MAX_DOMAIN_LEN 255
#define DNS_MAX_STR_DOMAIN_LEN 256
#define DNS_MAX_IPS 16
/*
 * 12 bytes header
 * 1+ bytes for domain name
 * 2 bytes type
 * 2 bytes class
 */
#define DNS_MIN_REQ_LEN 17

// A standard query (QUERY)
#define DNS_QUERY_OPCODE 0

#define IP_V4_SIZE 4
#define IP_V6_SIZE 16

/** @brief DNS packet header (12 bytes). */
typedef struct DnsHeader_s {
    uint16_t transaction_id;   ///< Transaction identifier.
    uint16_t flags;            ///< DNS flags and codes.
    uint16_t question_count;   ///< Number of question records.
    uint16_t answer_count;     ///< Number of answer records.
    uint16_t authority_count;  ///< Number of authority records.
    uint16_t additional_count; ///< Number of additional records.
} DnsHeader;

/** @brief DNS question section. */
typedef struct DnsQuestion_s {
    char domain[DNS_MAX_DOMAIN_LEN + 1];  ///< Queried domain name.
    uint16_t type;                        ///< Query type (A, AAAA, etc.).
    uint16_t qclass;                     ///< Query class (usually IN).
} DnsQuestion;

/** @brief DNS answer section containing resolved IPs. */
typedef struct DnsAnswer_s {
    char domain[DNS_MAX_DOMAIN_LEN + 1];  ///< Domain name for this answer.
    uint32_t ip_count;                    ///< Number of IPv4 addresses.
    uint32_t ips[DNS_MAX_IPS];           ///< IPv4 addresses (network byte order).
    uint32_t ip6_count;                  ///< Number of IPv6 addresses.
    uint8_t ip6s[DNS_MAX_IPS][16];      ///< IPv6 addresses.
} DnsAnswer;

/** @brief Complete parsed DNS packet. */
typedef struct DnsPacket_s {
    bool is_valid;         ///< Whether packet was successfully parsed.
    bool is_response;      ///< True if this is a DNS response, false if query.
    DnsHeader header;      ///< DNS header fields.
    DnsQuestion question;  ///< Question section.
    DnsAnswer answers[DNS_MAX_IPS];  ///< Answer records.
    uint32_t answer_count; ///< Number of answer records.
} DnsPacket;

/**
 * @brief Check if payload is a valid DNS packet.
 * @param[in] payload     Pointer to DNS data.
 * @param[in] payload_len Length of DNS data.
 * @return true if valid DNS packet, false otherwise.
 */
bool DnsIsDnsPacket(const uint8_t* payload, size_t payload_len);

/**
 * @brief Parse a DNS packet and extract domain and IP information.
 * @param[in] payload     Pointer to DNS data.
 * @param[in] payload_len Length of DNS data.
 * @return Parsed DnsPacket structure.
 */
DnsPacket DnsParse(const uint8_t* payload, size_t payload_len);

/**
 * @brief Clean up a parsed DNS packet.
 * @param[in,out] packet Pointer to DnsPacket to free.
 */
void DnsFree(DnsPacket* packet);

/**
 * @brief Check if domain matches any whitelist entry.
 * @param[in] domain          Domain name to check.
 * @param[in] whitelist       Array of whitelist entries.
 * @param[in] whitelist_count Number of entries in whitelist array.
 * @return 1 if whitelisted, 0 if not, -1 on error.
 */
int DnsCheckDomain(const char* domain, const char* whitelist[], size_t whitelist_count);

#endif
