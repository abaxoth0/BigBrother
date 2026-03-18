/**
 * @file encoding.c
 * @brief Simple message encoding/decoding protocol implementation.
 */

#include "encoding.h"
#include <string.h>
#include <stdio.h>

IpcMessageType EncodingParseMessageType(const char* str) {
    if (str == NULL) return -1;

    if (strcmp(str, "GET_WHITELIST") == 0) return MSG_GET_WHITELIST;
    if (strcmp(str, "SET_WHITELIST") == 0) return MSG_SET_WHITELIST;
    if (strcmp(str, "RELOAD") == 0) return MSG_RELOAD;
    if (strcmp(str, "GET_STATUS") == 0) return MSG_GET_STATUS;
    if (strcmp(str, "PING") == 0) return MSG_PING;
    if (strcmp(str, "WHITELIST") == 0) return MSG_WHITELIST;
    if (strcmp(str, "STATUS") == 0) return MSG_STATUS;
    if (strcmp(str, "OK") == 0) return MSG_OK;
    if (strcmp(str, "ERROR") == 0) return MSG_ERROR;

    return -1;
}

const char* EncodingMessageTypeToString(IpcMessageType type) {
    switch (type) {
        case MSG_GET_WHITELIST:  return "GET_WHITELIST";
        case MSG_SET_WHITELIST:  return "SET_WHITELIST";
        case MSG_RELOAD:         return "RELOAD";
        case MSG_GET_STATUS:     return "GET_STATUS";
        case MSG_WHITELIST:      return "WHITELIST";
        case MSG_STATUS:         return "STATUS";
        case MSG_OK:             return "OK";
        case MSG_ERROR:          return "ERROR";
        default:                 return "UNKNOWN";
    }
}

int EncodingFormatStatus(char* buffer, size_t size, size_t whitelist_count, size_t allowlist_count) {
    return snprintf(buffer, size, "STATUS\n%zu\n%zu\nrunning\n", whitelist_count, allowlist_count);
}

int EncodingFormatWhitelist(char* buffer, size_t size, const char** domains, size_t count) {
    size_t pos = 0;
    int n = snprintf(buffer + pos, size - pos, "WHITELIST\n");
    if (n < 0) return -1;
    pos += n;

    for (size_t i = 0; i < count && pos < size - 1; i++) {
        n = snprintf(buffer + pos, size - pos, "%s\n", domains[i]);
        if (n < 0) return -1;
        pos += n;
    }

    return (int)pos;
}

int EncodingFormatOk(char* buffer, size_t size) {
    return snprintf(buffer, size, "OK\n");
}

int EncodingFormatError(char* buffer, size_t size, const char* error) {
    return snprintf(buffer, size, "ERROR\n%s\n", error);
}
