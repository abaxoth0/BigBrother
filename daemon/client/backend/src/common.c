
#include <fileapi.h>
#include <libloaderapi.h>
#include <minwindef.h>
#include <stdarg.h>
#include <stdio.h>
#include "../include/common.h"

static FILE* g_LogFile = NULL;

void log_init(void) {
    char exe_path[MAX_PATH];
    char* name = exe_path + GetModuleFileName(NULL, exe_path, sizeof(exe_path));
    while (name > exe_path && *(name - 1) != '\\') name--;
    *name = '\0';

    char log_path[512];
    snprintf(log_path, sizeof(log_path), "%s\\client.log", exe_path);

    g_LogFile = fopen(log_path, "a");
    if (!g_LogFile) {
        char temp_path[MAX_PATH];
        GetTempPath(sizeof(temp_path), temp_path);
        snprintf(log_path, sizeof(log_path), "%sBigBrother_client.log", temp_path);
        g_LogFile = fopen(log_path, "a");
    }

    if (g_LogFile) {
        fprintf(g_LogFile, "[Client] Started, log: %s\n", log_path);
        fflush(g_LogFile);
    }
}

void log_msg(const char* fmt, ...) {
    if (!g_LogFile) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(g_LogFile, fmt, args);
    va_end(args);
    fflush(g_LogFile);
}
