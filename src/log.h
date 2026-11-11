#ifndef TSW_LOG_H
#define TSW_LOG_H

#include <stddef.h>

#define LOG_LINES 4
#define LOG_LINE_LEN 256

typedef struct {
    char lines[LOG_LINES][LOG_LINE_LEN];
    int count;
} LogBuffer;

void log_init(LogBuffer *log);
void log_add(LogBuffer *log, const char *fmt, ...);
const char *log_line(const LogBuffer *log, int index);

#endif
