#ifndef TSW_TEXT_LAYOUT_H
#define TSW_TEXT_LAYOUT_H

/* Word-wrapped, optionally justified, line layout.  Pure logic: the caller
 * measures text, so it is tested without a font (tests/test_text_layout.c)
 * and drawn by ui_text_justified (widgets.c).
 *
 * Words are runs of non-space bytes; a run of spaces is one gap and '\n'
 * ends a paragraph.  Lines are filled greedily.  Justified, every line but
 * a paragraph's last spreads its spare pixels over its gaps (the gaps
 * differ by at most one pixel, the wider ones first); a paragraph's last
 * line and a line of one word stay left-aligned.  A word wider than the
 * line gets a line of its own and overflows it (the caller clips). */

#include <stdbool.h>
#include <stddef.h>

/* Width in pixels of text[0..len). */
typedef int (*TextMeasure)(void *ctx, const char *text, size_t len);

typedef struct {
    size_t start;   /* byte offset into the text */
    size_t len;     /* bytes */
    int x;          /* pixels from the line's left edge */
    int line;       /* 0-based */
} TextWord;

/* Lays `text` out in lines `width` pixels wide.  Fills up to `max_words`
 * words and returns how many there are in all (words past `max_words` are
 * counted but not stored).  `*lines` gets the number of lines. */
int text_layout(const char *text, int width, bool justify,
                TextMeasure measure, void *ctx, TextWord *out,
                int max_words, int *lines);

#endif
