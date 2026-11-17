#include "text_layout.h"

/* Positions the words [first, end) of one line: left-aligned with natural
 * spaces, or justified to `width`. */
static void place_line(TextWord *w, int first, int end, const int *widths,
                       int space, int width, bool justify, int line)
{
    int n = end - first;
    if (n <= 0)
        return;
    int gaps = n - 1;
    int spare = 0;
    if (justify && gaps > 0) {
        int used = 0;
        for (int i = first; i < end; i++)
            used += widths[i - first];
        spare = width - used;
    }
    int x = 0;
    for (int i = first; i < end; i++) {
        int k = i - first;
        w[i].x = x;
        w[i].line = line;
        x += widths[k];
        if (k < gaps) {
            if (justify && gaps > 0)
                x += spare / gaps + (k < spare % gaps ? 1 : 0);
            else
                x += space;
        }
    }
}

int text_layout(const char *text, int width, bool justify,
                TextMeasure measure, void *ctx, TextWord *out,
                int max_words, int *lines)
{
    if (lines)
        *lines = 0;
    if (!text || !measure)
        return 0;
    int space = measure(ctx, " ", 1);

    /* Words of the line being filled; a line holds at most `max_words`
     * stored words, and widths are kept for those only. */
    enum { LINE_MAX = 256 };
    int widths[LINE_MAX];
    int count = 0;          /* words seen */
    int line = 0;
    int line_first = 0;     /* index of the line's first word */
    int line_used = 0;      /* natural width of the line so far */
    bool line_empty = true;
    bool any = false;

    const char *p = text;
    for (;;) {
        while (*p == ' ')
            p++;
        if (*p == '\n' || *p == '\0') {
            /* Paragraph end: its last line stays left-aligned. */
            if (!line_empty) {
                int end = count < max_words ? count : max_words;
                place_line(out, line_first, end, widths, space, width, false,
                           line);
                line++;
            } else if (*p == '\n') {
                line++;     /* an empty paragraph is an empty line */
            }
            line_empty = true;
            line_used = 0;
            line_first = count;
            if (*p == '\0')
                break;
            p++;
            continue;
        }
        const char *s = p;
        while (*p && *p != ' ' && *p != '\n')
            p++;
        size_t len = (size_t)(p - s);
        int ww = measure(ctx, s, len);
        any = true;

        int n_line = count - line_first;
        bool fits = line_empty
                    || (line_used + space + ww <= width && n_line < LINE_MAX);
        if (!fits) {
            int end = count < max_words ? count : max_words;
            place_line(out, line_first, end, widths, space, width, justify,
                       line);
            line++;
            line_first = count;
            line_used = 0;
            line_empty = true;
            n_line = 0;
        }
        if (count < max_words) {
            out[count].start = (size_t)(s - text);
            out[count].len = len;
            out[count].x = 0;
            out[count].line = line;
        }
        widths[n_line] = ww;
        line_used += (line_empty ? 0 : space) + ww;
        line_empty = false;
        count++;
    }
    if (lines)
        *lines = any ? line : 0;
    return count;
}
