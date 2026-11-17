#include "messages.h"

#include "fileio.h"

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
    size_t size = 0;
    char *data = file_load(path, &size);
    if (!data) {
        if (err && errsz)
            snprintf(err, errsz, "cannot open %s", path ? path : "(null)");
        return false;
    }
    t->text = calloc(MESSAGES_MAX, sizeof(char *));
    t->speaker = calloc(MESSAGES_MAX, sizeof(char *));
    if (!t->text || !t->speaker) {
        free(t->text);
        free(t->speaker);
        t->text = t->speaker = NULL;
        file_free(data);
        if (err && errsz) snprintf(err, errsz, "out of memory");
        return false;
    }
    char line[4096];
    int lineno = 0;
    bool ok = true;
    const char *cur = data;
    while (text_next_line(&cur, data + size, line, sizeof(line))) {
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
        const char *body = tab + 1;
        char *tab2 = strchr(body, '\t');
        free(t->speaker[index]);
        t->speaker[index] = NULL;
        if (tab2) {
            if (tab2 > body)
                t->speaker[index] = unescape(body, (size_t)(tab2 - body));
            body = tab2 + 1;
        }
        free(t->text[index]);
        t->text[index] = unescape(body, strlen(body));
        if ((int)index >= t->count)
            t->count = (int)index + 1;
    }
    file_free(data);
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
    if (t->speaker) {
        for (int i = 0; i < MESSAGES_MAX; i++)
            free(t->speaker[i]);
        free(t->speaker);
    }
    t->text = NULL;
    t->speaker = NULL;
    t->count = 0;
}

bool messages_path(char *out, size_t outsz, const char *data_dir,
                   const char *file)
{
    int n = snprintf(out, outsz, "%s/text/" MESSAGES_LANG "/%s", data_dir,
                     file);
    return n > 0 && (size_t)n < outsz;
}

const char *messages_get(const MessageTable *t, int index)
{
    if (!t || !t->text || index < 0 || index >= t->count)
        return NULL;
    return t->text[index];
}

const char *messages_speaker(const MessageTable *t, int index)
{
    if (!t || !t->speaker || index < 0 || index >= t->count)
        return NULL;
    return t->speaker[index];
}

/* --------------------------------------------------------------------- */
/* Speakers                                                               */

static const SpeakerDef SPEAKERS[] = {
    { "hero",          "Brave man",                -1,   -1   },
    { "thief",         "Thief",                    0x14, 0x15 },
    { "zeno",          "The Magic Sergeant, Zeno", 0x5a, 0x5b },
    { "golden_knight", "Golden Knight",            0x84, 0x85 },
    { "skeleton_a",    "Skeleton A",               0x50, 0x51 },
    { "vampire",       "Vampire",                  0x5e, 0x5f },
    { "master",        "Great Magic Master",       0x90, 0x91 },
    { "old_man",       "Old man",                  0x18, 0x19 },
    { "merchant",      "Merchant",                 0x12, 0x13 },
    { "fairy",         "Fairy",                    0x16, 0x17 },
    { "altar",         "Altar",                    0x0f, 0x11 },
};
#define SPEAKER_COUNT ((int)(sizeof(SPEAKERS) / sizeof(SPEAKERS[0])))

int speaker_count(void)
{
    return SPEAKER_COUNT;
}

const SpeakerDef *speaker_def(int speaker)
{
    return speaker >= 0 && speaker < SPEAKER_COUNT ? &SPEAKERS[speaker] : NULL;
}

int speaker_find(const char *id)
{
    if (!id || !id[0])
        return -1;
    for (int i = 0; i < SPEAKER_COUNT; i++)
        if (strcmp(SPEAKERS[i].id, id) == 0)
            return i;
    return -1;
}

void speaker_names_init(SpeakerNames *n)
{
    memset(n, 0, sizeof(*n));
    for (int i = 0; i < SPEAKER_COUNT && i < SPEAKER_MAX; i++)
        snprintf(n->name[i], sizeof(n->name[i]), "%s", SPEAKERS[i].name);
}

bool speaker_names_load(SpeakerNames *n, const char *path)
{
    size_t size = 0;
    char *data = file_load(path, &size);
    if (!data)
        return false;
    char line[512];
    bool ok = true;
    const char *cur = data;
    while (text_next_line(&cur, data + size, line, sizeof(line))) {
        size_t len = strcspn(line, "\r\n");
        line[len] = '\0';
        if (len == 0)
            continue;
        char *tab = strchr(line, '\t');
        if (!tab) {
            ok = false;
            continue;
        }
        *tab = '\0';
        int sp = speaker_find(line);
        if (sp < 0 || sp >= SPEAKER_MAX) {
            ok = false;
            continue;
        }
        char *name = unescape(tab + 1, strlen(tab + 1));
        if (name)
            snprintf(n->name[sp], sizeof(n->name[sp]), "%s", name);
        free(name);
    }
    file_free(data);
    return ok;
}

const char *speaker_name(const SpeakerNames *n, int speaker)
{
    if (!n || speaker < 0 || speaker >= SPEAKER_COUNT || speaker >= SPEAKER_MAX)
        return NULL;
    return n->name[speaker];
}
