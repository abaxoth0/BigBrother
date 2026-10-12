#include "../include/certificate_pin.h"
#include "../include/tls_client.h"
#include <ws2tcpip.h>
#include <ctype.h>
#include <string.h>

int bb_normalize_fingerprint(const char* input, char output[65]) {
    if (!input || !output) return -1;
    size_t n = 0;
    for (; *input; input++) {
        unsigned char c = (unsigned char)*input;
        if (c == ':' || isspace(c)) continue;
        if (!isxdigit(c) || n >= 64) { output[0] = '\0'; return -1; }
        output[n++] = (char)toupper(c);
    }
    output[n] = '\0';
    if (n != 64) { output[0] = '\0'; return -1; }
    return 0;
}

int bb_probe_certificate(const char* address, int port, const char* expected,
                         char fingerprint[65]) {
    fingerprint[0] = '\0';
    if (!address || !address[0] || port < 1 || port > 65535) return -1;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int result = -1;
    if (sock == INVALID_SOCKET) goto done;
    struct sockaddr_in target = {0};
    target.sin_family = AF_INET;
    target.sin_port = htons((unsigned short)port);
    const char* ip = strcmp(address, ".") == 0 ? "127.0.0.1" : address;
    if (inet_pton(AF_INET, ip, &target.sin_addr) != 1) goto close_socket;
    u_long nonblocking = 1;
    if (ioctlsocket(sock, FIONBIO, &nonblocking) != 0) goto close_socket;
    if (connect(sock, (struct sockaddr*)&target, sizeof(target)) != 0) {
        if (WSAGetLastError() != WSAEWOULDBLOCK) goto close_socket;
        fd_set write_set, error_set;
        FD_ZERO(&write_set); FD_SET(sock, &write_set);
        FD_ZERO(&error_set); FD_SET(sock, &error_set);
        struct timeval timeout = {3, 0};
        if (select(0, NULL, &write_set, &error_set, &timeout) <= 0 ||
            FD_ISSET(sock, &error_set)) goto close_socket;
        int error = 0, error_len = sizeof(error);
        if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&error, &error_len) != 0 || error)
            goto close_socket;
    }
    bb_tls_t tls;
    if (bb_tls_init(&tls) != 0) goto close_socket;
    result = bb_tls_connect(&tls, sock, expected, fingerprint, 65);
    bb_tls_free(&tls);
close_socket:
    closesocket(sock);
done:
    WSACleanup();
    return result;
}
