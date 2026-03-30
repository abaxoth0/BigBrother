
#include <fileapi.h>
#include <libloaderapi.h>
#include <minwindef.h>
#include <stdarg.h>
#include <stdio.h>
#include "../include/common.h"
#include "../../../common/log/log.h"

void log_init(void) {
    char exe_path[MAX_PATH];
    char* name = exe_path + GetModuleFileName(NULL, exe_path, sizeof(exe_path));
    while (name > exe_path && *(name - 1) != '\\') name--;
    *name = '\0';

    char log_path[512];
    snprintf(log_path, sizeof(log_path), "%s\\client.log", exe_path);

    LogFile = fopen(log_path, "a");
    if (!LogFile) {
        char temp_path[MAX_PATH];
        GetTempPath(sizeof(temp_path), temp_path);
        snprintf(log_path, sizeof(log_path), "%sBigBrother_client.log", temp_path);
        LogFile = fopen(log_path, "a");
    }

    if (LogFile) {
        LOGF("[Client] Started, log: %s", log_path);
    }
}
