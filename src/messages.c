#include "messages.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MESSAGES_MAX 4096

static char *unescape(const char *s, size_t n)
{
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

bool messages_load(MessageTable *t, const char *path, char *err, size_t errsz)
{
    memset(t, 0, sizeof(*t));
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) {
        if (err && errsz)
            snprintf(err, errsz, "cannot open %s", path ? path : "(null)");
        return false;
    }
    t->text = calloc(MESSAGES_MAX, sizeof(char *));
    if (!t->text) {
        fclose(f);
        if (err && errsz) snprintf(err, errsz, "out of memory");
        return false;
    }
    char line[4096];
    int lineno = 0;
    bool ok = true;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        size_t len = strcspn(line, "\r\n");
        line[len] = '\0';
        if (len == 0)
            continue;
        char *tab = strchr(line, '\t');
        char *end = NULL;
        long index = strtol(line, &end, 10);
        if (!tab || end != tab || index < 0 || index >= MESSAGES_MAX) {
            if (err && errsz)
                snprintf(err, errsz, "%s:%d: malformed line", path, lineno);
            ok = false;
            break;
        }
        free(t->text[index]);
        t->text[index] = unescape(tab + 1, strlen(tab + 1));
        if ((int)index >= t->count)
            t->count = (int)index + 1;
    }
    fclose(f);
    if (!ok)
        messages_free(t);
    return ok;
}

void messages_free(MessageTable *t)
{
    if (t->text) {
        for (int i = 0; i < MESSAGES_MAX; i++)
            free(t->text[i]);
        free(t->text);
    }
    t->text = NULL;
    t->count = 0;
}

const char *messages_get(const MessageTable *t, int index)
{
    if (!t || !t->text || index < 0 || index >= t->count)
        return NULL;
    return t->text[index];
}
