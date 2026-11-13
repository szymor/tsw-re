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

    log->scroll = 0;                     /* a new line shows the newest */
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

void log_scroll(LogBuffer *log, int n)
{
    if (!log) return;
    int max = log->count > LOG_VISIBLE ? log->count - LOG_VISIBLE : 0;
    log->scroll += n;
    if (log->scroll < 0) log->scroll = 0;
    if (log->scroll > max) log->scroll = max;
}

const char *log_line(const LogBuffer *log, int index)
{
    if (!log || index < 0 || index >= log->count)
        return "";
    return log->lines[index];
}
