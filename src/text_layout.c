#include "text_layout.h"

/* Positions the words [first, end) of one line: left-aligned with their
 * natural gaps (a space, or none between characters of a script written
 * without spaces), or justified to `width` (the spare pixels spread over
 * all the gaps). */
static void place_line(TextWord *w, int first, int end, const int *widths,
                       const int *gapw, int width, bool justify, int line)
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
                x += gapw[k + 1];
        }
    }
}

/* The code point at `p` (UTF-8) and its length in `*len`. */
static unsigned decode(const char *p, int *len)
{
    const unsigned char *u = (const unsigned char *)p;
    if (u[0] < 0x80) { *len = 1; return u[0]; }
    if ((u[0] & 0xe0) == 0xc0 && (u[1] & 0xc0) == 0x80) {
        *len = 2;
        return ((u[0] & 0x1fu) << 6) | (u[1] & 0x3fu);
    }
    if ((u[0] & 0xf0) == 0xe0 && (u[1] & 0xc0) == 0x80 &&
        (u[2] & 0xc0) == 0x80) {
        *len = 3;
        return ((u[0] & 0x0fu) << 12) | ((u[1] & 0x3fu) << 6) | (u[2] & 0x3fu);
    }
    if ((u[0] & 0xf8) == 0xf0 && (u[1] & 0xc0) == 0x80 &&
        (u[2] & 0xc0) == 0x80 && (u[3] & 0xc0) == 0x80) {
        *len = 4;
        return ((u[0] & 0x07u) << 18) | ((u[1] & 0x3fu) << 12) |
               ((u[2] & 0x3fu) << 6) | (u[3] & 0x3fu);
    }
    *len = 1;
    return u[0];
}

/* Scripts written without spaces (CJK): a line may break between any two
 * of their characters. */
static bool unspaced(unsigned c)
{
    return (c >= 0x2e80 && c <= 0x9fff) || (c >= 0xf900 && c <= 0xfaff) ||
           (c >= 0xff00 && c <= 0xffef) || (c >= 0x20000 && c <= 0x3ffff);
}

/* Kinsoku: characters that never start a line (closing punctuation,
 * small kana, the long-vowel mark) and that never end one (opening
 * brackets). */
static bool no_break_before(unsigned c)
{
    static const unsigned L[] = {
        0x3001, 0x3002, 0xff0c, 0xff0e, 0x30fb, 0xff1a, 0xff1b, 0xff1f,
        0xff01, 0x30fc, 0x300d, 0x300f, 0xff09, 0xff3d, 0xff5d, 0x3009,
        0x300b, 0x3011, 0x3015, 0x2026, 0x2025, 0x3005, 0x309d, 0x309e,
        0x30fd, 0x30fe, 0xff5e, 0x301c, 0x3041, 0x3043, 0x3045, 0x3047,
        0x3049, 0x3063, 0x3083, 0x3085, 0x3087, 0x308e, 0x30a1, 0x30a3,
        0x30a5, 0x30a7, 0x30a9, 0x30c3, 0x30e3, 0x30e5, 0x30e7, 0x30ee,
        0x30f5, 0x30f6, '.', ',', '!', '?', ')', ']', '}', ':', ';', '\'',
    };
    for (size_t i = 0; i < sizeof(L) / sizeof(L[0]); i++)
        if (L[i] == c)
            return true;
    return false;
}

static bool no_break_after(unsigned c)
{
    return c == 0x300c || c == 0x300e || c == 0xff08 || c == 0xff3b ||
           c == 0xff5b || c == 0x3008 || c == 0x300a || c == 0x3010 ||
           c == 0x3014 || c == '(' || c == '[' || c == '{';
}

/* The end of the unbreakable piece of [s, end) that starts at `s`: the
 * whole run for spaced scripts, one character (with what kinsoku binds
 * to it) where a CJK character allows a break. */
static const char *piece_end(const char *s, const char *end)
{
    int n;
    unsigned prev = decode(s, &n);
    const char *q = s + n;
    while (q < end) {
        int m;
        unsigned c = decode(q, &m);
        if ((unspaced(prev) || unspaced(c)) && !no_break_before(c) &&
            !no_break_after(prev))
            break;
        prev = c;
        q += m;
    }
    return q;
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
    int gapw[LINE_MAX];     /* the gap before each word: space or none */
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
                place_line(out, line_first, end, widths, gapw, width, false,
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
        const char *run = p;
        while (*p && *p != ' ' && *p != '\n')
            p++;
        /* The run is one word in spaced scripts; in CJK text it splits
         * into pieces that join without a gap. */
        for (const char *s = run; s < p;) {
            const char *e = piece_end(s, p);
            size_t len = (size_t)(e - s);
            int ww = measure(ctx, s, len);
            int gap = s == run ? space : 0;
            any = true;

            int n_line = count - line_first;
            bool fits = line_empty
                        || (line_used + gap + ww <= width && n_line < LINE_MAX);
            if (!fits) {
                int end = count < max_words ? count : max_words;
                place_line(out, line_first, end, widths, gapw, width, justify,
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
            gapw[n_line] = line_empty ? 0 : gap;
            line_used += gapw[n_line] + ww;
            line_empty = false;
            count++;
            s = e;
        }
    }
    if (lines)
        *lines = any ? line : 0;
    return count;
}
