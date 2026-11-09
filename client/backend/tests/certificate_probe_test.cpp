#include "../include/certificate_pin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    char fingerprint[65];
    const char* expected = strcmp(argv[2], "inspect") == 0 ? "" : argv[2];
    int result = bb_probe_certificate("127.0.0.1", atoi(argv[1]), expected, fingerprint);
    if (fingerprint[0]) puts(fingerprint);
    return result == 0 ? 0 : 1;
}
