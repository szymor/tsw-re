#ifndef TSW_LOG_H
#define TSW_LOG_H

#include <stddef.h>

#define LOG_LINES 200            /* console history */
#define LOG_VISIBLE 4            /* lines that fit under the map */
#define LOG_LINE_LEN 256

typedef struct {
    char lines[LOG_LINES][LOG_LINE_LEN];
    int count;
    int scroll;                  /* lines scrolled back from the newest */
} LogBuffer;

void log_init(LogBuffer *log);
void log_add(LogBuffer *log, const char *fmt, ...);
const char *log_line(const LogBuffer *log, int index);
/* Scroll the console back (+) or forward (-) by n lines. */
void log_scroll(LogBuffer *log, int n);

#endif
