/**
 * @file certificate_pin.h
 * @brief Certificate fingerprint normalization and inspection.
 */

#ifndef BB_CERTIFICATE_PIN_H
#define BB_CERTIFICATE_PIN_H

/**
 * @brief Remove spaces/colons and write a 64-digit uppercase SHA-256 fingerprint.
 * @return 0 on success, -1 on invalid input. output must hold 65 bytes.
 */
int bb_normalize_fingerprint(const char* input, char output[65]);
/**
 * @brief Inspect a server certificate without sending application commands or persisting trust.
 * @param address IPv4 text, or "." for loopback.
 * @param port TCP port in 1–65535.
 * @param expected Expected fingerprint, or NULL/empty to inspect without a pin.
 * @param fingerprint Output buffer for the observed fingerprint, including on a pin mismatch.
 * @return 0 on successful handshake and pin check, -1 on failure.
 */
int bb_probe_certificate(const char* address, int port, const char* expected,
                         char fingerprint[65]);

#endif
