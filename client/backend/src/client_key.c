#include "../include/client_key.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <mbedtls/error.h>
#include <mbedtls/sha256.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <psa/crypto.h>

#include "../../../common/log/log.h"

#define KEY_FILE_PRIVATE_LEN 32
#define KEY_FILE_POINT_LEN 65
#define KEY_FILE_TOTAL (KEY_FILE_PRIVATE_LEN + KEY_FILE_POINT_LEN)

/* In-memory RNG seeded from OS entropy, reused by keygen + sign. */
static mbedtls_entropy_context g_ent;
static mbedtls_ctr_drbg_context g_drbg;
static int g_rng_ready = 0;

static int rng_ready(void) {
    if (g_rng_ready) return 0;
    mbedtls_entropy_init(&g_ent);
    mbedtls_ctr_drbg_init(&g_drbg);
    if (mbedtls_ctr_drbg_seed(&g_drbg, mbedtls_entropy_func, &g_ent,
                              (const unsigned char*)"bb-client-key", 13) != 0) {
        return -1;
    }
    g_rng_ready = 1;
    return 0;
}

static void hex_encode(const unsigned char* in, size_t n, char* out) {
    static const char* digs = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[i * 2] = digs[in[i] >> 4];
        out[i * 2 + 1] = digs[in[i] & 0x0F];
    }
    out[n * 2] = '\0';
}

/* --- minimal DER helper (definite-length single-byte; payloads < 128) --- */

/* Builds ECPrivateKey SEC1 DER from scalar + uncompressed point:
 *   SEQUENCE { INTEGER 1, OCTET STRING d, [0] { OID }, [1] { BIT STRING point } }
 */
static int build_sec1(const unsigned char* d, const unsigned char* point,
                      unsigned char* out, size_t cap) {
    const unsigned char oid[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07}; /* 1.2.840.10045.3.1.7 */
    unsigned char body[256];
    size_t b = 0;

    /* version INTEGER 1 */
    body[b++] = 0x02; body[b++] = 0x01; body[b++] = 0x01;
    /* d OCTET STRING (32) */
    body[b++] = 0x04; body[b++] = 0x20;
    memcpy(body + b, d, 32); b += 32;
    /* parameters [0] { OID } */
    body[b++] = 0xA0; body[b++] = (unsigned char)(sizeof(oid) + 2);
    body[b++] = 0x06; body[b++] = (unsigned char)sizeof(oid);
    memcpy(body + b, oid, sizeof(oid)); b += sizeof(oid);
    /* publicKey [1] { BIT STRING (0x00 || point) } */
    body[b++] = 0xA1; body[b++] = (unsigned char)(65 + 1 + 2); /* unused-bits byte + point + tag+len */
    body[b++] = 0x03; body[b++] = (unsigned char)(65 + 1);     /* 66 */
    body[b++] = 0x00;
    memcpy(body + b, point, 65); b += 65;

    if (cap < b + 2) return -1;
    out[0] = 0x30;
    out[1] = (unsigned char)b;
    memcpy(out + 2, body, b);
    return 0;
}

static int write_key_file(const char* path, const unsigned char* d, const unsigned char* point) {
    unsigned char data[KEY_FILE_TOTAL];
    memcpy(data, d, 32);
    memcpy(data + 32, point, 65);
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    size_t w = fwrite(data, 1, sizeof(data), f);
    fclose(f);
    return w == sizeof(data) ? 0 : -1;
}

static int generate_and_persist(const char* path, mbedtls_pk_context* pk) {
    if (rng_ready() != 0) return -1;
    if (psa_crypto_init() != PSA_SUCCESS) return -1;

    psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_SIGN_MESSAGE | PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attrs, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_set_key_type(&attrs, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attrs, 256);

    psa_key_id_t id = 0;
    if (psa_generate_key(&attrs, &id) != PSA_SUCCESS) return -1;

    unsigned char priv[64];
    size_t priv_len = 0;
    if (psa_export_key(id, priv, sizeof(priv), &priv_len) != PSA_SUCCESS ||
        priv_len != 32) {
        psa_destroy_key(id);
        return -1;
    }

    unsigned char spki[256];
    size_t spki_len = 0;
    if (psa_export_public_key(id, spki, sizeof(spki), &spki_len) != PSA_SUCCESS ||
        spki_len < 65) {
        psa_destroy_key(id);
        return -1;
    }
    unsigned char* point = spki + spki_len - 65;
    psa_destroy_key(id);

    if (write_key_file(path, priv, point) != 0) return -1;

    unsigned char sec1[256];
    if (build_sec1(priv, point, sec1, sizeof(sec1)) != 0) {
        return -1;
    }
    mbedtls_pk_init(pk);
    if (mbedtls_pk_parse_key(pk, sec1, (size_t)(2 + sec1[1]), NULL, 0, NULL, NULL) != 0) {
        mbedtls_pk_free(pk);
        return -1;
    }
    return 0;
}

static int load_key(const char* path, mbedtls_pk_context* pk) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    unsigned char data[KEY_FILE_TOTAL];
    size_t rd = fread(data, 1, sizeof(data), f);
    fclose(f);
    if (rd != sizeof(data)) return -1;

    const unsigned char* d = data;
    const unsigned char* point = data + 32;
    unsigned char sec1[256];
    if (build_sec1(d, point, sec1, sizeof(sec1)) != 0) {
        return -1;
    }
    mbedtls_pk_init(pk);
    if (mbedtls_pk_parse_key(pk, sec1, (size_t)(2 + sec1[1]), NULL, 0, NULL, NULL) != 0) {
        mbedtls_pk_free(pk);
        return -1;
    }
    return 0;
}

int client_key_ensure(const char* path, mbedtls_pk_context* pk) {
    if (load_key(path, pk) == 0) return 0;
    return generate_and_persist(path, pk);
}

int client_key_public_point_hex(const mbedtls_pk_context* pk, char* out, size_t cap) {
    unsigned char der[256];
    int dlen = mbedtls_pk_write_pubkey_der(pk, der, sizeof(der));
    if (dlen < 65) return -1;
    const unsigned char* point = der + sizeof(der) - 65;
    if (cap < 131) return -1;
    hex_encode(point, 65, out);
    return 0;
}

int client_key_sign(mbedtls_pk_context* pk, const unsigned char* msg,
                    size_t mlen, char* out, size_t cap) {
    if (rng_ready() != 0) return -1;
    unsigned char digest[32];
    mbedtls_sha256(msg, mlen, digest, 0);

    unsigned char sig[128];
    size_t sig_len = 0;
    if (mbedtls_pk_sign(pk, MBEDTLS_MD_SHA256, digest, sizeof(digest),
                        sig, sizeof(sig), &sig_len,
                        mbedtls_ctr_drbg_random, &g_drbg) != 0) {
        return -1;
    }
    if (cap < sig_len * 2 + 1) return -1;
    hex_encode(sig, sig_len, out);
    return 0;
}