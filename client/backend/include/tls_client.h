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

typedef struct {
    SOCKET sock;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    int connected;
} bb_tls_t;

/* Read return codes: >0 bytes read; 0 = clean close; -2 = would-block/timeout
 * (transient, retry); -1 = hard error. */
#define BB_TLS_TRY_AGAIN (-2)

/* Initialize (call once before reuse). */
int bb_tls_init(bb_tls_t* t);

/* Connect + TLS handshake over an already-connected TCP socket, verifying the
 * server certificate's SHA-256 fingerprint.
 *   pinned_fp_hex: expected 64-char hex fingerprint, or "" for trust-on-first-use.
 *   out_fp_hex:    receives the peer fingerprint (>= 65 bytes).
 * Returns 0 on success, -1 on failure (mismatch or handshake error). */
int bb_tls_connect(bb_tls_t* t, SOCKET sock, const char* pinned_fp_hex,
                   char* out_fp_hex, size_t out_sz);

int bb_tls_read(bb_tls_t* t, void* buf, size_t len);
int bb_tls_write(bb_tls_t* t, const void* buf, size_t len);
void bb_tls_free(bb_tls_t* t);

#ifdef __cplusplus
}
#endif

#endif /* BB_TLS_CLIENT_H */