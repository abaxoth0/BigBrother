/**
 * @file tls_client.h
 * @brief TLS connections with explicit fingerprint verification.
 */

#ifndef BB_TLS_CLIENT_H
#define BB_TLS_CLIENT_H

#include <winsock2.h>
#include <stddef.h>

#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief TLS state borrowing a caller-owned socket; do not copy or use concurrently. */
typedef struct {
    SOCKET sock;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    int connected;
} bb_tls_t;

/** @brief Transient TLS read result; retry when the socket is ready. */
#define BB_TLS_TRY_AGAIN (-2)

/** @brief Initialize TLS state before connecting; returns 0 on success, -1 on setup failure. */
int bb_tls_init(bb_tls_t* t);

/**
 * @brief Complete TLS over an already-connected socket and check the certificate fingerprint.
 * @param t Initialized TLS context; release with bb_tls_free(), including after failure.
 * @param sock Borrowed connected TCP socket; the caller remains responsible for closesocket().
 * @param pinned_fp_hex Expected SHA-256 hex fingerprint, or NULL/empty to skip pin verification.
 * @param out_fp_hex Optional output for the observed fingerprint; no trust is persisted here.
 * @param out_sz Output capacity; at least 65 bytes are needed for the full fingerprint.
 * @return 0 on success, -1 on handshake or pin-verification failure.
 */
int bb_tls_connect(bb_tls_t* t, SOCKET sock, const char* pinned_fp_hex,
                   char* out_fp_hex, size_t out_sz);

/** @brief Read plaintext; returns bytes read, 0 for close, BB_TLS_TRY_AGAIN for retry, or -1 on error. */
int bb_tls_read(bb_tls_t* t, void* buf, size_t len);
/** @brief Retry partial/transient writes until all bytes are sent; returns bytes written or -1. */
int bb_tls_write(bb_tls_t* t, const void* buf, size_t len);
/** @brief Release TLS state and send close-notify without closing the borrowed TCP socket. */
void bb_tls_free(bb_tls_t* t);

#ifdef __cplusplus
}
#endif

#endif /* BB_TLS_CLIENT_H */