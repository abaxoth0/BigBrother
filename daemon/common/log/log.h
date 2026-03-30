#ifndef LOG_H
#define LOG_H

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <string.h>

extern FILE* LogFile;

#define LOGF_BUF_SIZE 4096

static char logf_buf[LOGF_BUF_SIZE] __attribute__((unused));

#define LOG_TYPE_LOG  1
#define LOG_TYPE_DLOG 2

#define LOG_LEVEL_INFO   0
#define LOG_LEVEL_ERROR  1
#define LOG_LEVEL_DEBUG  2
#define LOG_LEVEL_BLOCKED 3

#define LOGF_HEADER_SIZE 4

static void __attribute__((unused)) log_write(uint16_t type, uint8_t level, const char* msg) {
    if (!LogFile) return;
    
    uint64_t timestamp = (uint64_t)time(NULL);
    uint16_t msg_len = (uint16_t)strlen(msg) + 1;
    uint16_t payload_len = 8 + 1 + msg_len; // timestamp + level + message
    
    uint8_t header[4];
    header[0] = (type >> 0) & 0xFF;
    header[1] = (type >> 8) & 0xFF;
    header[2] = (payload_len >> 0) & 0xFF;
    header[3] = (payload_len >> 8) & 0xFF;
    
    fwrite(header, 1, 4, LogFile);
    
    uint8_t payload[512];
    int i = 0;
    // timestamp (8 bytes)
    for (int j = 0; j < 8; j++) {
        payload[i++] = (timestamp >> (j * 8)) & 0xFF;
    }
    // level (1 byte)
    payload[i++] = level;
    // message (null-terminated)
    memcpy(&payload[i], msg, msg_len);
    i += msg_len;
    
    fwrite(payload, 1, payload_len, LogFile);
    fflush(LogFile);
}

#define LOGF(...) do { \
    sprintf(logf_buf, __VA_ARGS__);  \
    log_write(LOG_TYPE_LOG, LOG_LEVEL_INFO, logf_buf); \
} while(0)

#define LOGE(...) do { \
    sprintf(logf_buf, __VA_ARGS__);  \
    log_write(LOG_TYPE_LOG, LOG_LEVEL_ERROR, logf_buf); \
} while(0)

#define LOGB(...) do { \
    sprintf(logf_buf, __VA_ARGS__);  \
    log_write(LOG_TYPE_LOG, LOG_LEVEL_BLOCKED, logf_buf); \
} while(0)

#ifdef DEBUG

#define DLOGF(...) do { \
    sprintf(logf_buf, __VA_ARGS__);  \
    log_write(LOG_TYPE_DLOG, LOG_LEVEL_DEBUG, logf_buf); \
    OutputDebugString(logf_buf); \
} while(0)

#else // ifdef DEBUG

#define DLOGF(...)

#endif // ifdef DEBUG

#endif // LOG_H
