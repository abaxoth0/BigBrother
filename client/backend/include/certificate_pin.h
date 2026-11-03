#ifndef BB_CERTIFICATE_PIN_H
#define BB_CERTIFICATE_PIN_H

int bb_normalize_fingerprint(const char* input, char output[65]);
// TLS inspection only: sends no application commands and persists no trust.
int bb_probe_certificate(const char* address, int port, const char* expected,
                         char fingerprint[65]);

#endif
