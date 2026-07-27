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

int get_server_port(void);
int is_discovery_enabled(void);

int GetLocalIp(const char* server_ip, char* buffer, size_t buffer_size);
int GetAllBroadcastAddresses(char* ip_str, size_t ip_size, char* bcast_out, size_t bcast_size);
int DiscoverServers(const char* bcast_list, int port, int timeout_ms, char* out, size_t out_size);
int DiscoverServersTCP(char* out, size_t out_size, int timeout_ms);

#endif
