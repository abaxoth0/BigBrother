#include "log.h"
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>

FILE* LogFile = NULL;
LoggerContext* g_logger = NULL;

static uint32_t align_up(uint32_t size, uint32_t align) {
    return (size + align - 1) & ~(align - 1);
}

static DWORD WINAPI logger_thread_func(LPVOID param);
static void flush_batch(LoggerContext* ctx);
static void rotate_log(LoggerContext* ctx);

int log_init_async(uint32_t buffer_size) {
    if (g_logger) {
        return 0;  // Already initialized
    }

    // Round up buffer size to page size (4KB)
    buffer_size = align_up(buffer_size, 4096);
    if (buffer_size < 4096) buffer_size = 4096;

    // Allocate logger context
    g_logger = (LoggerContext*)malloc(sizeof(LoggerContext));
    if (!g_logger) {
        return -1;
    }
    memset(g_logger, 0, sizeof(LoggerContext));

    // Allocate ring buffer (extra 8 bytes for head/tail)
    size_t rb_size = sizeof(RingBuffer) + buffer_size;
    g_logger->rb = (RingBuffer*)malloc(rb_size);
    if (!g_logger->rb) {
        free(g_logger);
        g_logger = NULL;
        return -1;
    }

    memset(g_logger->rb, 0, rb_size);
    g_logger->rb->capacity = buffer_size;
    g_logger->rb->head = 0;
    g_logger->rb->tail = 0;

    // Create semaphore for write signaling
    g_logger->write_semaphore = CreateSemaphore(NULL, 0, LOG_BATCH_SIZE, NULL);
    if (!g_logger->write_semaphore) {
        free(g_logger->rb);
        free(g_logger);
        g_logger = NULL;
        return -1;
    }

    g_logger->running = 1;
    g_logger->last_flush_time = GetTickCount();  // Initialize to current time so timer works
    g_logger->max_file_size = LOG_MAX_FILE_SIZE;
    g_logger->max_files = LOG_MAX_FILES;
    g_logger->current_file_size = 0;

    // NOTE: Do NOT set default path here - caller must set it via g_logger->log_path
    // before using LOGF. The logger thread will open the file when it starts.

    // Create logger thread
    g_logger->logger_thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)logger_thread_func, g_logger, 0, NULL);
    if (!g_logger->logger_thread) {
        CloseHandle(g_logger->write_semaphore);
        free(g_logger->rb);
        free(g_logger);
        g_logger = NULL;
        return -1;
    }

    return 0;
}

static DWORD WINAPI logger_thread_func(LPVOID param) {
    LoggerContext* ctx = (LoggerContext*)param;
    RingBuffer* rb = ctx->rb;

    // Open log file for batch writing
    ctx->batch_file = fopen(ctx->log_path, "ab");
    if (!ctx->batch_file) {
        ctx->running = 0;
        return 1;
    }

    // Get current file size for rotation tracking
    fseek(ctx->batch_file, 0, SEEK_END);
    ctx->current_file_size = (uint32_t)ftell(ctx->batch_file);

    DWORD flush_interval_ms = 200;  // Flush every 200ms for better responsiveness
    
    while (ctx->running || rb->tail != rb->head) {
        // Check if we need to flush based on timer (only if we have entries)
        if (ctx->entry_count > 0 && ctx->last_flush_time > 0) {
            DWORD elapsed = GetTickCount() - ctx->last_flush_time;
            if (elapsed >= flush_interval_ms) {
                flush_batch(ctx);
            }
        }

        // Check for new entries
        if (rb->tail == rb->head) {
            if (ctx->running) {
                Sleep(10);  // Small sleep to prevent busy spinning
            }
            continue;
        }

        // Read entry from ring buffer (4 byte header: type + payload_len)
        uint32_t tail = rb->tail;
        uint16_t payload_len = *(uint16_t*)&rb->data[tail + 2];

        uint32_t next_tail = tail + 4 + payload_len;
        if (next_tail >= rb->capacity) {
            next_tail = 0;  // Wrap around
        }

        // Copy entry to batch buffer (4 header + payload)
        memcpy(&ctx->batch_buffer[ctx->entry_count * 512], &rb->data[tail], 4 + payload_len);
        ctx->entry_count++;

        // Update tail
        rb->tail = next_tail;

        // Flush if batch is full or timer elapsed
        if (ctx->entry_count >= LOG_BATCH_SIZE) {
            flush_batch(ctx);
        }
        
        // Also check timer even if batch not full
        if (ctx->entry_count > 0) {
            DWORD elapsed = GetTickCount() - ctx->last_flush_time;
            if (elapsed >= flush_interval_ms) {
                flush_batch(ctx);
            }
        }
    }

    // Final flush (only if there are remaining entries)
    if (ctx->entry_count > 0) {
        flush_batch(ctx);
    }

    fclose(ctx->batch_file);
    ctx->running = 0;

    return 0;
}

static void rotate_log(LoggerContext* ctx) {
    if (ctx->max_file_size == 0) return;
    if (ctx->current_file_size < ctx->max_file_size) return;

    fclose(ctx->batch_file);
    ctx->batch_file = NULL;

    if (LogFile) {
        fclose(LogFile);
        LogFile = NULL;
    }

    // Extract directory and base name from log_path
    char dir[512] = {0};
    char base[256] = {0};
    char ext[64] = {0};

    const char* last_sep = strrchr(ctx->log_path, '\\');
    if (!last_sep) last_sep = strrchr(ctx->log_path, '/');

    if (last_sep) {
        size_t dir_len = last_sep - ctx->log_path;
        memcpy(dir, ctx->log_path, dir_len);
        dir[dir_len] = '\0';

        const char* dot = strrchr(last_sep + 1, '.');
        if (dot) {
            size_t base_len = dot - (last_sep + 1);
            memcpy(base, last_sep + 1, base_len);
            base[base_len] = '\0';
            strncpy(ext, dot, sizeof(ext) - 1);
        } else {
            strncpy(base, last_sep + 1, sizeof(base) - 1);
        }
    } else {
        const char* dot = strrchr(ctx->log_path, '.');
        if (dot) {
            size_t base_len = dot - ctx->log_path;
            memcpy(base, ctx->log_path, base_len);
            base[base_len] = '\0';
            strncpy(ext, dot, sizeof(ext) - 1);
        } else {
            strncpy(base, ctx->log_path, sizeof(base) - 1);
        }
    }

    // Build archive directory path (dir + \archive can exceed 512, so use 1024)
    char archive_dir[1024];
    snprintf(archive_dir, sizeof(archive_dir), "%s\\archive", dir);
    CreateDirectory(archive_dir, NULL);

    // Path buffers: archive_dir (~520) + base (~255) + ext (~63) + separators/number + null
    // Compiler worst-case: 520 + 1 + 255 + 1 + 10 + 63 + 1 = 851, but it computes ~1354
    // Use 2048 to silence -Wformat-truncation
    char old_path[2048];
    char new_path[2048];
    char tmp_path[1024];

    snprintf(tmp_path, sizeof(tmp_path), "%s\\%s.rotating", dir, base);

    // Copy current log to temp (CopyFile works even when file is open by others)
    if (!CopyFile(ctx->log_path, tmp_path, FALSE)) {
        DWORD err = GetLastError();
        LOGF("[LogRotation] CopyFile failed: %lu", err);
        ctx->batch_file = fopen(ctx->log_path, "ab");
        if (!ctx->batch_file) { ctx->running = 0; return; }
        LogFile = fopen(ctx->log_path, "ab");
        return;
    }

    // Delete oldest rotated file
    snprintf(old_path, sizeof(old_path), "%s\\%s_%u%s", archive_dir, base, ctx->max_files, ext);
    DeleteFile(old_path);

    // Shift archive_N -> archive_N+1
    for (uint32_t i = ctx->max_files - 1; i >= 1; i--) {
        snprintf(old_path, sizeof(old_path), "%s\\%s_%u%s", archive_dir, base, i, ext);
        snprintf(new_path, sizeof(new_path), "%s\\%s_%u%s", archive_dir, base, i + 1, ext);
        if (!MoveFile(old_path, new_path)) {
            DWORD err = GetLastError();
            if (err != ERROR_FILE_NOT_FOUND) {
                CopyFile(old_path, new_path, FALSE);
                DeleteFile(old_path);
            }
        }
    }

    // Temp copy -> archive_1
    snprintf(new_path, sizeof(new_path), "%s\\%s_1%s", archive_dir, base, ext);
    MoveFile(tmp_path, new_path);

    // Truncate original log file (works even if someone has it open)
    ctx->batch_file = fopen(ctx->log_path, "wb");
    if (!ctx->batch_file) {
        LOGF("[LogRotation] Failed to truncate log file: %s", ctx->log_path);
        ctx->running = 0;
        return;
    }

    LogFile = fopen(ctx->log_path, "ab");

    LOGF("[LogRotation] Rotated %s (was %u bytes)", ctx->log_path, ctx->current_file_size);
    ctx->current_file_size = 0;
}

static void flush_batch(LoggerContext* ctx) {
    if (ctx->entry_count == 0 || !ctx->batch_file) {
        ctx->entry_count = 0;
        return;
    }

    // Check if rotation is needed before writing
    rotate_log(ctx);
    if (!ctx->batch_file) {
        ctx->entry_count = 0;
        return;
    }

    // Write all entries to file (4 byte header: type + payload_len)
    uint32_t bytes_written = 0;
    for (int i = 0; i < ctx->entry_count; i++) {
        uint16_t payload_len = *(uint16_t*)&ctx->batch_buffer[i * 512 + 2];
        uint32_t entry_size = 4 + payload_len;
        fwrite(&ctx->batch_buffer[i * 512], 1, entry_size, ctx->batch_file);
        bytes_written += entry_size;
    }
    fflush(ctx->batch_file);
    ctx->current_file_size += bytes_written;
    ctx->entry_count = 0;
    ctx->last_flush_time = GetTickCount();
}

void log_shutdown(void) {
    if (!g_logger) {
        return;
    }

    g_logger->running = 0;

    // Wait for logger thread to finish
    if (g_logger->logger_thread) {
        WaitForSingleObject(g_logger->logger_thread, 5000);
        CloseHandle(g_logger->logger_thread);
    }

    if (g_logger->write_semaphore) {
        CloseHandle(g_logger->write_semaphore);
    }

    if (g_logger->rb) {
        free(g_logger->rb);
    }

    free(g_logger);
    g_logger = NULL;
}

void log_write_async(uint16_t type, uint8_t level, const char* msg) {
    if (!g_logger || !g_logger->running) {
        return;
    }

    RingBuffer* rb = g_logger->rb;

    uint64_t timestamp = (uint64_t)time(NULL);
    uint16_t msg_len = (uint16_t)strlen(msg) + 1;
    uint16_t payload_len = 8 + 1 + msg_len;  // timestamp + level + message

    // Total size: 4 (type+len header) + payload
    uint16_t total_size = 4 + payload_len;

    // Check available space (blocking)
    while (1) {
        uint32_t head = rb->head;
        uint32_t tail = rb->tail;

        uint32_t available;
        if (head >= tail) {
            available = rb->capacity - (head - tail) - 1;
        } else {
            available = (tail - head) - 1;
        }

        if (available >= total_size) {
            break;
        }

        // Buffer full - wait a bit and retry
        Sleep(1);
    }

    // Write entry to ring buffer
    uint32_t head = rb->head;

    // Write TYPE and payload_len header
    *(uint16_t*)&rb->data[head] = type;
    *(uint16_t*)&rb->data[head + 2] = payload_len;
    uint32_t offset = head + 4;

    // Write payload
    int i = 0;
    // timestamp (8 bytes)
    for (int j = 0; j < 8; j++) {
        rb->data[offset + i++] = (timestamp >> (j * 8)) & 0xFF;
    }
    // level (1 byte)
    rb->data[offset + i++] = level;
    // message (null-terminated)
    memcpy(&rb->data[offset + i], msg, msg_len);
    i += msg_len;

    // Update head (total = 4 header + payload)
    uint32_t new_head = head + 4 + payload_len;
    if (new_head >= rb->capacity) {
        new_head = 0;  // Wrap around
    }
    rb->head = new_head;

    // Signal logger thread
    ReleaseSemaphore(g_logger->write_semaphore, 1, NULL);
}

void log_write_sync(uint16_t type, uint8_t level, const char* msg) {
    if (!LogFile) return;
    
    uint64_t timestamp = (uint64_t)time(NULL);
    uint16_t msg_len = (uint16_t)strlen(msg) + 1;
    uint16_t payload_len = 8 + 1 + msg_len;
    
    uint8_t header[4];
    header[0] = (type >> 0) & 0xFF;
    header[1] = (type >> 8) & 0xFF;
    header[2] = (payload_len >> 0) & 0xFF;
    header[3] = (payload_len >> 8) & 0xFF;
    
    fwrite(header, 1, 4, LogFile);
    
    uint8_t payload[512];
    int idx = 0;
    for (int j = 0; j < 8; j++) {
        payload[idx++] = (timestamp >> (j * 8)) & 0xFF;
    }
    payload[idx++] = level;
    memcpy(&payload[idx], msg, msg_len);
    idx += msg_len;
    
    fwrite(payload, 1, payload_len, LogFile);
    fflush(LogFile);
}

// Legacy function for backward compatibility
static void __attribute__((unused)) log_write(uint16_t type, uint8_t level, const char* msg) {
    if (g_logger && g_logger->running) {
        log_write_async(type, level, msg);
    } else {
        log_write_sync(type, level, msg);
    }
}
