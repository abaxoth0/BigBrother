#ifndef LOG_H
#define LOG_H

#include <stdarg.h>
#include <stdio.h>

extern FILE* LogFile;

#define LOGF_BUF_SIZE 4096

static char logf_buf[LOGF_BUF_SIZE] __attribute__((unused));

#define LOGF(...) do { \
    sprintf(logf_buf, __VA_ARGS__);  \
    if (LogFile) fputs(logf_buf, LogFile); \
} while(0)

#ifdef DEBUG

#define DLOGF(...) do {          \
    LOGF(__VA_ARGS__);           \
    OutputDebugString(logf_buf); \
} while(0)

#else // ifdef DEBUG

#define DLOGF(...)

#endif // ifdef DEBUG

#endif // LOG_H
