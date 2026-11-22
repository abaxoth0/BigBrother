#ifndef BB_CLIENT_KEY_H
#define BB_CLIENT_KEY_H

#include <stddef.h>

#include <mbedtls/pk.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file client_key.h
 * @brief Persistent P-256 identity and challenge signing for token delivery.
 */

/**
 * @brief Load a signing key, creating and persisting one only when the file is absent.
 * Existing unreadable/corrupt keys are never replaced. The caller supplies an
 * initialized context and releases it with mbedtls_pk_free().
 * @return 0 on success, -1 on key, permission or I/O failure.
 */
int client_key_ensure(const char* path, mbedtls_pk_context* pk);

/**
 * @brief Encode the uncompressed public point as hex (130 digits plus NUL).
 * @return 0 on success, -1 if export fails or cap is less than 131.
 */
int client_key_public_point_hex(const mbedtls_pk_context* pk, char* out, size_t cap);

/**
 * @brief Sign msg using ECDSA-P256/SHA-256 and write the DER signature as NUL-terminated hex.
 * @return 0 on success, -1 on signing failure or insufficient output capacity.
 */
int client_key_sign(mbedtls_pk_context* pk, const unsigned char* msg, size_t mlen, char* out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* BB_CLIENT_KEY_H */