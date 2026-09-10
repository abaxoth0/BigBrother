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
    if (strcmp(str, "GET_FILTRATION") == 0) return MSG_GET_FILTRATION;
    if (strcmp(str, "SET_FILTRATION") == 0) return MSG_SET_FILTRATION;

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

int EncodingFormatOk(char* buffer, size_t size) {
    return snprintf(buffer, size, "OK\n");
}

int EncodingFormatError(char* buffer, size_t size, const char* error) {
    return snprintf(buffer, size, "ERROR\n%s\n", error);
}
