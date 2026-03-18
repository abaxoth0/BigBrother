/**
 * @file encoding.h
 * @brief Simple message encoding/decoding protocol for IPC.
 * 
 * Message format:
 * - First line: message type ID
 * - Subsequent lines: data fields
 * - Fields separated by '\n'
 */

#ifndef ENCODING_H
#define ENCODING_H

#include <stddef.h>

#define ENCODING_MAX_LINE_LENGTH 256
#define ENCODING_BUFFER_SIZE 4096

/**
 * @brief Message types for Daemon IPC.
 */
typedef enum {
    MSG_GET_WHITELIST,
    MSG_SET_WHITELIST,
    MSG_RELOAD,
    MSG_GET_STATUS,
    MSG_PING,
    MSG_WHITELIST,
    MSG_STATUS,
    MSG_OK,
    MSG_ERROR
} IpcMessageType;

/**
 * @brief Parse message type from a string.
 * 
 * @param str String to parse (should be null-terminated).
 * 
 * @return Message type, or -1 if unknown.
 */
IpcMessageType EncodingParseMessageType(const char* str);

/**
 * @brief Get string representation of message type.
 * 
 * @param type Message type.
 * 
 * @return String representation (do not free).
 */
const char* EncodingMessageTypeToString(IpcMessageType type);

/**
 * @brief Format STATUS response.
 * 
 * @param buffer Output buffer.
 * @param size Buffer size.
 * @param whitelist_count Number of whitelisted domains.
 * @param allowlist_count Number of allowed IPs.
 * 
 * @return Number of bytes written, or -1 on error.
 */
int EncodingFormatStatus(char* buffer, size_t size, size_t whitelist_count, size_t allowlist_count);

/**
 * @brief Format WHITELIST response.
 * 
 * @param buffer Output buffer.
 * @param size Buffer size.
 * @param domains Array of domain strings.
 * @param count Number of domains.
 * 
 * @return Number of bytes written, or -1 on error.
 */
int EncodingFormatWhitelist(char* buffer, size_t size, const char** domains, size_t count);

/**
 * @brief Format OK response.
 * 
 * @param buffer Output buffer.
 * @param size Buffer size.
 * 
 * @return Number of bytes written, or -1 on error.
 */
int EncodingFormatOk(char* buffer, size_t size);

/**
 * @brief Format ERROR response.
 * 
 * @param buffer Output buffer.
 * @param size Buffer size.
 * @param error Error message.
 * 
 * @return Number of bytes written, or -1 on error.
 */
int EncodingFormatError(char* buffer, size_t size, const char* error);

#endif // ENCODING_H
