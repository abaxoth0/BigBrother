#include "../include/tls_client.h"

#include <string.h>
#include <stdio.h>

#include <mbedtls/error.h>
#include <mbedtls/sha256.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/debug.h>
#include <mbedtls/psa_util.h>
#include <psa/crypto.h>

#include "../../../common/log/log.h"

static void tls_log_ret(int ret, const char* what) {
    char err[256];
    mbedtls_strerror(ret, err, sizeof(err));
    LOGF("[TLS] %s failed: -0x%04X %s", what, (unsigned int)(-ret), err);
}

static int net_send_cb(void* ctx, const unsigned char* buf, size_t len) {
    SOCKET s = *(SOCKET*)ctx;
    int n = send(s, (const char*)buf, (int)(len > 0x7FFFFFFF ? 0x7FFFFFFF : len), 0);
    if (n == SOCKET_ERROR) {
        int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK || e == WSAETIMEDOUT) {
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        }
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return n;
}

static int net_recv_cb(void* ctx, unsigned char* buf, size_t len) {
    SOCKET s = *(SOCKET*)ctx;
    int n = recv(s, (char*)buf, (int)(len > 0x7FFFFFFF ? 0x7FFFFFFF : len), 0);
    if (n == 0) {
        return 0; /* clean close */
    }
    if (n == SOCKET_ERROR) {
        int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK || e == WSAETIMEDOUT) {
            return MBEDTLS_ERR_SSL_WANT_READ;
        }
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    return n;
}

static void hex_upper(const unsigned char* in, size_t n, char* out) {
    static const char* digs = "0123456789ABCDEF";
    for (size_t i = 0; i < n; i++) {
        out[i * 2] = digs[in[i] >> 4];
        out[i * 2 + 1] = digs[in[i] & 0x0F];
    }
    out[n * 2] = '\0';
}

int bb_tls_init(bb_tls_t* t) {
    if (!t) return -1;
    memset(t, 0, sizeof(*t));
    t->sock = INVALID_SOCKET;
    t->connected = 0;
    mbedtls_entropy_init(&t->entropy);
    mbedtls_ctr_drbg_init(&t->drbg);
    mbedtls_ssl_init(&t->ssl);
    mbedtls_ssl_config_init(&t->conf);
    return 0;
}

int bb_tls_connect(bb_tls_t* t, SOCKET sock, const char* pinned_fp_hex,
                   char* out_fp_hex, size_t out_sz) {
    int ret;
    const char* pers = "bigbrother-client";

    t->sock = sock;

#if defined(MBEDTLS_PSA_CRYPTO_C)
    /* TLS 1.3 in mbedTLS 3.x uses PSA crypto internally; it must be initialized
       before any handshake, otherwise the handshake fails with an internal
       error (-0x6C00). */
    psa_status_t psa_ret = psa_crypto_init();
    if (psa_ret != PSA_SUCCESS) {
        LOGF("[TLS] PSA crypto init failed: %d", (int)psa_ret);
        return -1;
    }
#endif

    /* Seed RNG from OS entropy. */
    ret = mbedtls_ctr_drbg_seed(&t->drbg, mbedtls_entropy_func, &t->entropy,
                                (const unsigned char*)pers, strlen(pers));
    if (ret != 0) {
        tls_log_ret(ret, "RNG seed");
        return -1;
    }

    ret = mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        tls_log_ret(ret, "ssl config");
        return -1;
    }
    /* No CA: we pin the server's certificate fingerprint ourselves. */
    mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
    mbedtls_ssl_conf_rng(&t->conf, mbedtls_ctr_drbg_random, &t->drbg);

    /* Pin TLS 1.2: the TLS1.3 code path fails with an internal error on some
       Windows setups; the server supports 1.2, so this keeps a working secure
       channel (pinned cert + token) without the 1.3 path. */
    mbedtls_ssl_conf_min_version(&t->conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_max_version(&t->conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);

    ret = mbedtls_ssl_setup(&t->ssl, &t->conf);
    if (ret != 0) {
        tls_log_ret(ret, "ssl setup");
        return -1;
    }
    /* set_hostname is required for TLS1.3 and harmless for 1.2. */
    ret = mbedtls_ssl_set_hostname(&t->ssl, "BigBrotherServer");
    if (ret != 0) {
        tls_log_ret(ret, "set_hostname");
        return -1;
    }

    mbedtls_ssl_set_bio(&t->ssl, &t->sock, net_send_cb, net_recv_cb, NULL);

    ret = mbedtls_ssl_handshake(&t->ssl);
    if (ret != 0) {
        tls_log_ret(ret, "handshake");
        return -1;
    }

    /* Compute the peer certificate SHA-256 fingerprint and verify the pin. */
    const mbedtls_x509_crt* peer = mbedtls_ssl_get_peer_cert(&t->ssl);
    if (!peer) {
        LOGF("[TLS] no peer certificate from server");
        return -1;
    }
    unsigned char hash[32];
    ret = mbedtls_sha256(peer->raw.p, peer->raw.len, hash, 0);
    if (ret != 0) {
        tls_log_ret(ret, "fingerprint");
        return -1;
    }
    char fp[65];
    hex_upper(hash, sizeof(hash), fp);

    if (out_fp_hex && out_sz > 0) {
        strncpy(out_fp_hex, fp, out_sz - 1);
        out_fp_hex[out_sz - 1] = '\0';
    }

    if (pinned_fp_hex && pinned_fp_hex[0] != '\0') {
        if (_stricmp(pinned_fp_hex, fp) != 0) {
            LOGF("[TLS] fingerprint mismatch: pinned=%s peer=%s", pinned_fp_hex, fp);
            return -1; /* fingerprint mismatch */
        }
    }

    t->connected = 1;
    return 0;
}

int bb_tls_read(bb_tls_t* t, void* buf, size_t len) {
    if (!t || !t->connected) return -1;
    int n = mbedtls_ssl_read(&t->ssl, (unsigned char*)buf, len);
    if (n > 0) return n;
    if (n == 0) return 0; /* clean close */
    if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
        return BB_TLS_TRY_AGAIN;
    }
    return -1;
}

int bb_tls_write(bb_tls_t* t, const void* buf, size_t len) {
    if (!t || !t->connected) return -1;
    size_t written = 0;
    while (written < len) {
        int n = mbedtls_ssl_write(&t->ssl,
                                  (const unsigned char*)buf + written,
                                  len - written);
        if (n > 0) {
            written += (size_t)n;
            continue;
        }
        if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
            /* Blocking socket under mbedTLS shouldn't hit this often; spin to
             * complete the write, which the blocking recv/send guarantees. */
            continue;
        }
        return -1;
    }
    return (int)written;
}

void bb_tls_free(bb_tls_t* t) {
    if (!t) return;
    mbedtls_ssl_close_notify(&t->ssl);
    mbedtls_ssl_free(&t->ssl);
    mbedtls_ssl_config_free(&t->conf);
    mbedtls_ctr_drbg_free(&t->drbg);
    mbedtls_entropy_free(&t->entropy);
    t->connected = 0;
}