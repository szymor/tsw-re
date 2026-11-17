#include "text_layout.h"

#include <stdio.h>
#include <string.h>

/* A fixed-width "font": 10 px per character, 4 px per space. */
static int measure(void *ctx, const char *text, size_t len)
{
    (void)ctx;
    if (len == 1 && text[0] == ' ')
        return 4;
    return (int)len * 10;
}

static int failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static int layout(const char *text, int width, bool justify, TextWord *w,
                  int max, int *lines)
{
    return text_layout(text, width, justify, measure, NULL, w, max, lines);
}

static void test_breaks_and_last_line(void)
{
    TextWord w[8];
    int lines = 0;
    /* aa bb = 44 fits 60, cc would make 68. */
    int n = layout("aa bb cc dd", 60, true, w, 8, &lines);
    CHECK(n == 4 && lines == 2);
    CHECK(w[0].line == 0 && w[0].x == 0);
    CHECK(w[1].line == 0 && w[1].x == 40);   /* justified: ends at 60 */
    CHECK(w[2].line == 1 && w[2].x == 0);
    CHECK(w[3].line == 1 && w[3].x == 24);   /* last line: natural space */
}

static void test_gap_spreading(void)
{
    TextWord w[8];
    int lines = 0;
    /* Line 0 is a b c (30 px of words, 11 spare over two gaps: 6 then 5). */
    int n = layout("a b c dddd", 41, true, w, 8, &lines);
    CHECK(n == 4 && lines == 2);
    CHECK(w[1].x == 16);
    CHECK(w[2].x == 31);
    CHECK(w[2].x + 10 == 41);
    CHECK(w[3].line == 1 && w[3].x == 0);
}

static void test_left_aligned(void)
{
    TextWord w[8];
    int lines = 0;
    int n = layout("a b c dddd", 41, false, w, 8, &lines);
    CHECK(n == 4 && lines == 2);
    CHECK(w[1].x == 14 && w[2].x == 28);
}

static void test_single_and_long_words(void)
{
    TextWord w[8];
    int lines = 0;
    /* aaaaa alone (the next word does not fit), the long word on its own
     * line overflowing it, then c. */
    int n = layout("aaaaa bbbbbbbbbbbb c", 60, true, w, 8, &lines);
    CHECK(n == 3 && lines == 3);
    for (int i = 0; i < 3; i++)
        CHECK(w[i].line == i && w[i].x == 0);
}

static void test_spaces_and_offsets(void)
{
    TextWord w[8];
    int lines = 0;
    const char *t = "  aa   bb  ";
    int n = layout(t, 100, true, w, 8, &lines);
    CHECK(n == 2 && lines == 1);
    CHECK(w[0].start == 2 && w[0].len == 2);
    CHECK(w[1].start == 7 && w[1].len == 2);
    CHECK(w[1].x == 24);                     /* runs of spaces: one gap */
}

static void test_paragraphs(void)
{
    TextWord w[8];
    int lines = 0;
    int n = layout("aa bb\ncc\n\ndd", 1000, true, w, 8, &lines);
    CHECK(n == 4 && lines == 4);
    CHECK(w[1].line == 0 && w[1].x == 24);   /* paragraph end: not spread */
    CHECK(w[2].line == 1 && w[2].x == 0);
    CHECK(w[3].line == 3);                   /* empty paragraph: a line */
}

static void test_limits(void)
{
    TextWord w[2];
    int lines = -1;
    CHECK(layout("a b c", 100, true, w, 2, &lines) == 3);
    CHECK(lines == 1);
    CHECK(layout("", 100, true, w, 2, &lines) == 0 && lines == 0);
    CHECK(layout("   ", 100, true, w, 2, &lines) == 0 && lines == 0);
}

/* Every justified line but the last ends exactly at the edge, and its gaps
 * differ by at most one pixel. */
static void test_long_paragraph(void)
{
    const char *t = "What a relief you're okay! You were caught and thrown "
                    "into this prison by the Magic Sergeant. You were "
                    "unconscious when you got here.";
    TextWord w[64];
    int lines = 0;
    const int width = 230;
    int n = layout(t, width, true, w, 64, &lines);
    CHECK(n > 0 && n <= 64 && lines > 2);
    for (int i = 0; i < n; i++) {
        bool last_of_line = i + 1 == n || w[i + 1].line != w[i].line;
        int end = w[i].x + (int)w[i].len * 10;
        if (last_of_line && w[i].line < lines - 1 && w[i].x > 0)
            CHECK(end == width);
        CHECK(end <= width);
        if (!last_of_line && i + 2 < n && w[i + 2].line == w[i].line) {
            int g1 = w[i + 1].x - end;
            int g2 = w[i + 2].x - (w[i + 1].x + (int)w[i + 1].len * 10);
            CHECK(g1 - g2 >= 0 && g1 - g2 <= 1);
        }
    }
}

int main(void)
{
    test_breaks_and_last_line();
    test_gap_spreading();
    test_left_aligned();
    test_single_and_long_words();
    test_spaces_and_offsets();
    test_paragraphs();
    test_limits();
    test_long_paragraph();
    /* Japanese: no spaces, a break between any two characters, no gap
     * between them; kinsoku keeps 。 off a line's start and 「 off its
     * end.  Each kana is 3 bytes, so 30 px here. */
    {
        TextWord w[16];
        int lines = 0;
        int n = layout("\xe3\x81\x82\xe3\x81\x84\xe3\x81\x86"
                       "\xe3\x81\x88\xe3\x81\x8a", 70, false, w, 16,
                       &lines);                        /* あいうえお */
        CHECK(n == 5 && lines == 3);
        CHECK(w[0].x == 0 && w[1].x == 30 && w[1].line == 0);
        CHECK(w[2].line == 1 && w[2].x == 0 && w[4].line == 2);
        /* あい。う in 60 px: 。 stays with い, so い。 moves down whole */
        n = layout("\xe3\x81\x82\xe3\x81\x84\xe3\x80\x82"
                   "\xe3\x81\x86", 60, false, w, 16, &lines);
        CHECK(n == 3 && lines == 3);
        CHECK(w[1].len == 6 && w[1].line == 1);
        /* あ「い」 in 60 px: 「 goes down with い, not left at a line end */
        n = layout("\xe3\x81\x82\xe3\x80\x8c\xe3\x81\x84"
                   "\xe3\x80\x8d", 60, false, w, 16, &lines);
        CHECK(n == 2 && w[1].len == 9 && w[1].line == 1);
        /* Latin words in the same text keep their spaces. */
        n = layout("ab \xe3\x81\x82\xe3\x81\x84", 200, false, w, 16,
                   &lines);
        CHECK(n == 3 && w[1].x == 24 && w[2].x == 54);
    }

    if (failures) {
        fprintf(stderr, "test_text_layout: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_text_layout: ok\n");
    return 0;
}
