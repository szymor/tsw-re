#include "itext.h"

#include "fileio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const KEYS[ITX_COUNT] = {
#define ITEXT_KEY(id, key, en) key,
    ITEXT_LIST(ITEXT_KEY)
#undef ITEXT_KEY
};

static const char *const ENGLISH[ITX_COUNT] = {
#define ITEXT_EN(id, key, en) en,
    ITEXT_LIST(ITEXT_EN)
#undef ITEXT_EN
};

/* Loaded text (owned), NULL = the English. */
static char *g_text[ITX_COUNT];

const char *tx(ITextId id)
{
    if ((int)id < 0 || id >= ITX_COUNT)
        return "";
    return g_text[id] ? g_text[id] : ENGLISH[id];
}

const char *itext_key(ITextId id)
{
    return (int)id >= 0 && id < ITX_COUNT ? KEYS[id] : "";
}

const char *itext_english(ITextId id)
{
    return (int)id >= 0 && id < ITX_COUNT ? ENGLISH[id] : "";
}

void itext_reset(void)
{
    for (int i = 0; i < ITX_COUNT; i++) {
        free(g_text[i]);
        g_text[i] = NULL;
    }
}

/* The conversions of a printf format, one letter each ('d', 's', 'L' for
 * an ll/l length), "%%" skipped; false when one is not understood. */
static bool conversions(const char *s, char *out, size_t outsz)
{
    size_t n = 0;
    for (; *s; s++) {
        if (*s != '%')
            continue;
        s++;
        if (*s == '%')
            continue;
        while (*s && strchr("-+ #0123456789.", *s))
            s++;
        bool wide = false;
        while (*s == 'l') {
            wide = true;
            s++;
        }
        if (!*s || !strchr("dius", *s))
            return false;
        if (n + 2 >= outsz)
            return false;
        out[n++] = wide ? 'L' : (*s == 's' ? 's' : 'd');
    }
    out[n] = '\0';
    return true;
}

bool itext_same_conversions(const char *text, const char *english)
{
    char a[32], b[32];
    return text && english && conversions(text, a, sizeof(a)) &&
           conversions(english, b, sizeof(b)) && strcmp(a, b) == 0;
}

static char *unescape(const char *s)
{
    size_t n = strlen(s);
    char *out = malloc(n + 1);
    if (!out)
        return NULL;
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n) {
            char c = s[++i];
            out[j++] = c == 'n' ? '\n' : c == 't' ? '\t' : c;
        } else {
            out[j++] = s[i];
        }
    }
    out[j] = '\0';
    return out;
}

static void note(char *err, size_t errsz, bool *ok, const char *fmt,
                 const char *path, int lineno, const char *what)
{
    if (*ok && err && errsz)
        snprintf(err, errsz, fmt, path, lineno, what);
    *ok = false;
}

bool itext_load(const char *path, char *err, size_t errsz)
{
    size_t size = 0;
    char *data = file_load(path, &size);
    if (!data) {
        if (err && errsz)
            snprintf(err, errsz, "cannot open %s", path ? path : "(null)");
        return false;
    }
    char line[1024];
    int lineno = 0;
    bool ok = true;
    const char *cur = data;
    while (text_next_line(&cur, data + size, line, sizeof(line))) {
        lineno++;
        size_t len = strcspn(line, "\r\n");
        line[len] = '\0';
        if (len == 0 || line[0] == '#')
            continue;
        char *tab = strchr(line, '\t');
        if (!tab) {
            note(err, errsz, &ok, "%s:%d: no tab%s", path, lineno, "");
            continue;
        }
        *tab = '\0';
        int id = -1;
        for (int i = 0; i < ITX_COUNT; i++)
            if (strcmp(KEYS[i], line) == 0) {
                id = i;
                break;
            }
        if (id < 0) {
            note(err, errsz, &ok, "%s:%d: unknown key %s", path, lineno, line);
            continue;
        }
        char *text = unescape(tab + 1);
        if (!text)
            continue;
        if (!itext_same_conversions(text, ENGLISH[id])) {
            note(err, errsz, &ok, "%s:%d: %s must keep the %%d/%%s of the "
                 "English text", path, lineno, KEYS[id]);
            free(text);
            continue;
        }
        free(g_text[id]);
        g_text[id] = text;
    }
    file_free(data);
    return ok;
}
