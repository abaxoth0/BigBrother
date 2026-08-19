#ifndef LOG_H
#define LOG_H

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <windows.h>

extern FILE* LogFile;

#define LOGF_BUF_SIZE 4096

#define LOG_TYPE_LOG  1
#define LOG_TYPE_DLOG 2

#define LOG_LEVEL_INFO   0
#define LOG_LEVEL_ERROR  1
#define LOG_LEVEL_DEBUG  2
#define LOG_LEVEL_BLOCKED 3

#define LOGF_HEADER_SIZE 4

#define LOG_PATH_MAX 4096

#define LOG_BUFFER_SIZE (10 * 1024 * 1024)  // 10MB default
#define LOG_BATCH_SIZE 32
#define LOG_MAX_FILE_SIZE (10 * 1024 * 1024)  // 10MB default max log file size
#define LOG_MAX_FILES 10  // Keep last 10 rotated files + 1 current

#define RING_BUFFER_SIZE (LOG_BUFFER_SIZE + 4096)  // buffer + header space

typedef struct {
    volatile uint32_t head;  // written by producer
    volatile uint32_t tail;  // written by consumer
    uint32_t capacity;
    uint8_t  data[1];  // flexible array, actual size is capacity
} RingBuffer;

typedef struct {
    RingBuffer* rb;
    HANDLE logger_thread;
    HANDLE write_semaphore;
    volatile int running;
    char log_path[LOG_PATH_MAX];
    FILE* batch_file;
    int entry_count;
    uint32_t last_flush_time;
    uint8_t batch_buffer[LOG_BATCH_SIZE * 512];  // 512 bytes per entry max
    uint32_t max_file_size;  // Max size before rotation (bytes)
    uint32_t max_files;      // Number of rotated files to keep
    uint32_t current_file_size;  // Track current file size
} LoggerContext;

extern LoggerContext* g_logger;

// Initialize synchronous logging (for backward compatibility)
void log_init(void);

// Initialize async logging (starts logger thread)
// buffer_size: minimum buffer size (will be rounded up to page size)
// Returns 0 on success, -1 on failure
int log_init_async(uint32_t buffer_size);

void log_shutdown(void);

void log_write_async(uint16_t type, uint8_t level, const char* msg);

// Synchronous write for emergency mode
void log_write_sync(uint16_t type, uint8_t level, const char* msg);

#define LOGF(...) do { \
    char _logf_buf[LOGF_BUF_SIZE];  \
    snprintf(_logf_buf, LOGF_BUF_SIZE, __VA_ARGS__);  \
    log_write_async(LOG_TYPE_LOG, LOG_LEVEL_INFO, _logf_buf); \
} while(0)

#define LOGE(...) do { \
    char _logf_buf[LOGF_BUF_SIZE];  \
    snprintf(_logf_buf, LOGF_BUF_SIZE, __VA_ARGS__);  \
    log_write_async(LOG_TYPE_LOG, LOG_LEVEL_ERROR, _logf_buf); \
} while(0)

#define LOGB(...) do { \
    char _logf_buf[LOGF_BUF_SIZE];  \
    snprintf(_logf_buf, LOGF_BUF_SIZE, __VA_ARGS__);  \
    log_write_async(LOG_TYPE_LOG, LOG_LEVEL_BLOCKED, _logf_buf); \
} while(0)

#ifdef DEBUG

#define DLOGF(...) do { \
    char _logf_buf[LOGF_BUF_SIZE];  \
    snprintf(_logf_buf, LOGF_BUF_SIZE, __VA_ARGS__);  \
    log_write_async(LOG_TYPE_DLOG, LOG_LEVEL_DEBUG, _logf_buf); \
    OutputDebugString(_logf_buf); \
} while(0)

#else

#define DLOGF(...)

#endif

#endif // LOG_H
