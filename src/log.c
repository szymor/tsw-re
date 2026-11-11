#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

void log_init(LogBuffer *log)
{
    if (!log) return;
    memset(log, 0, sizeof(*log));
}

void log_add(LogBuffer *log, const char *fmt, ...)
{
    if (!log || !fmt) return;

    char message[LOG_LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);

    char stamp[16] = "";
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (tm)
        strftime(stamp, sizeof(stamp), "%H:%M:%S", tm);

    int slot;
    if (log->count < LOG_LINES) {
        slot = log->count++;
    } else {
        memmove(log->lines[0], log->lines[1],
                sizeof(log->lines[0]) * (LOG_LINES - 1));
        slot = LOG_LINES - 1;
    }
    int n = snprintf(log->lines[slot], sizeof(log->lines[slot]),
                     "[%s] ", stamp);
    if (n < 0 || (size_t)n >= sizeof(log->lines[slot]))
        return;
    size_t used = (size_t)n;
    snprintf(log->lines[slot] + used, sizeof(log->lines[slot]) - used,
             "%.*s", (int)(sizeof(log->lines[slot]) - used - 1), message);
}

const char *log_line(const LogBuffer *log, int index)
{
    if (!log || index < 0 || index >= log->count)
        return "";
    return log->lines[index];
}
