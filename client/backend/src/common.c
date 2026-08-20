#include <fileapi.h>
#include <libloaderapi.h>
#include <minwindef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../include/common.h"
#include "../../../common/log/log.h"

void log_init(void) {
    char exe_path[MAX_PATH];
    char* name = exe_path + GetModuleFileName(NULL, exe_path, sizeof(exe_path));
    while (name > exe_path && *(name - 1) != '\\') name--;
    *name = '\0';

    char logs_dir[LOG_PATH_MAX];
    snprintf(logs_dir, sizeof(logs_dir), "%s\\logs", exe_path);
    CreateDirectory(logs_dir, NULL);

    char log_path[LOG_PATH_MAX];
    snprintf(log_path, sizeof(log_path), "%s\\client.binlog", logs_dir);
    
    // Determine actual path (in case first one fails)
    if (fopen(log_path, "a") == NULL) {
        char temp_path[LOG_PATH_MAX];
        GetTempPath(sizeof(temp_path), temp_path);
        snprintf(log_path, sizeof(log_path), "%sBigBrother_client.binlog", temp_path);
    }
    
    // Initialize async logger with path set FIRST
    log_init_async(LOG_BUFFER_SIZE);
    
    // Must set path AFTER log_init_async creates g_logger
    extern LoggerContext* g_logger;
    if (g_logger) {
        snprintf(g_logger->log_path, sizeof(g_logger->log_path), "%s", log_path);
        g_logger->max_file_size = LOG_MAX_FILE_SIZE;
        g_logger->max_files = LOG_MAX_FILES;
    }
    
    // Also keep LogFile for any direct writes
    FILE* f = fopen(log_path, "a");
    if (f) {
        LogFile = f;
    }
    
    // Now write log
    LOGF("[Client] Started, log: %s", log_path);
}