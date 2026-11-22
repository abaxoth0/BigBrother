/**
 * @file net_client.h
 * @brief IPv4 interface selection and server discovery.
 */

#ifndef NET_CLIENT_H
#define NET_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#define DISCOVERY_PORT 42069
#define DISCOVERY_MAGIC "BIGBROTHER_DISCOVERY"
#define DISCOVERY_RESPONSE_MAGIC "BIGBROTHER_DISCOVERY_RESPONSE"
#define DISCOVERY_TIMEOUT_MS 2000
#define DISCOVERY_BUF_SIZE 4096
#define DISCOVERY_TCP_PORT 1984
#define DISCOVERY_TCP_TIMEOUT_MS 100
#define DISCOVERY_MAX_CONCURRENT 16
#define DISCOVERY_SCAN_TIMEOUT_MS 3000
#define DISCOVERY_MAX_SERVERS 32

/** @brief Read the configured server port, falling back to 1984. */
int get_server_port(void);
/** @brief Return whether network discovery is enabled (enabled by default). */
int is_discovery_enabled(void);

/**
 * @brief Write the local IPv4 address used to reach server_ip, falling back to hostname lookup.
 * @return 0 on success, -1 if lookup fails or the output buffer is invalid.
 */
int GetLocalIp(const char* server_ip, char* buffer, size_t buffer_size);
/**
 * @brief Write the local IPv4 address and newline-separated discovery broadcast targets.
 * Optional outputs may be NULL; non-NULL outputs require positive capacities.
 * @return Number of broadcast targets; zero when disabled or unavailable.
 */
int GetAllBroadcastAddresses(char* ip_str, size_t ip_size, char* bcast_out, size_t bcast_size);
/**
 * @brief Discover servers by UDP broadcast within timeout_ms.
 * @param bcast_list Newline-separated IPv4 broadcast addresses.
 * @param port Destination UDP port.
 * @param timeout_ms Receive timeout in milliseconds.
 * @param out Buffer receiving newline-separated server records.
 * @param out_size Output capacity including NUL.
 * @return Number of discovered servers, or zero on failure/no replies.
 */
int DiscoverServers(const char* bcast_list, int port, int timeout_ms, char* out, size_t out_size);
/**
 * @brief Scan local subnets for TCP discovery replies within timeout_ms.
 * @return Number of discovered servers written as newline-separated records, or zero.
 */
int DiscoverServersTCP(char* out, size_t out_size, int timeout_ms);

#endif
