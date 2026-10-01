#ifndef BB_CLIENT_KEY_H
#define BB_CLIENT_KEY_H

#include <stddef.h>

#include <mbedtls/pk.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Point + scalar persistence plus ECDSA-P256 signing for the automatic
 * token-delivery handshake. Public point is shared at REGISTER; the private
 * key signs a server challenge to authorize token delivery. */

/* Loads the client signing key, generating+persisting a fresh P-256 keypair
 * if none exists. Returns 0 on success and fills *pk (caller frees it with
 * mbedtls_pk_free). */
int client_key_ensure(const char* path, mbedtls_pk_context* pk);

/* Compares stored public point stored in pk-size context via pk_parse. */
int client_key_public_point_hex(const mbedtls_pk_context* pk, char* out, size_t cap);

/* Signs msg with ECDSA-P256/SHA-256, writing the DER signature as hex. */
int client_key_sign(mbedtls_pk_context* pk, const unsigned char* msg, size_t mlen, char* out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* BB_CLIENT_KEY_H */