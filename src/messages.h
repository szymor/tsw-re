#ifndef TSW_MESSAGES_H
#define TSW_MESSAGES_H

/* The game text, one folder per language: data/text/<lang>/messages.tsv is
 * the dialogue list (the original's TTSW10.ListBox2.Items, form field
 * +0x44c), indexed exactly like the original (docs/re/18-messages.md), and
 * ui.tsv the literals of the code.  The files are the source to edit;
 * tools/extract_messages.py re-extracts the original English from TSW.exe
 * for comparison.  SDL-free. */

#include <stdbool.h>
#include <stddef.h>

/* The language of the game text (English only, for now). */
#define MESSAGES_LANG "en"

typedef struct {
    char **text;
    int count;
} MessageTable;

/* "<data_dir>/text/<lang>/<file>"; false when it does not fit. */
bool messages_path(char *out, size_t outsz, const char *data_dir,
                   const char *file);
/* Load "<index>\t<escaped text>" lines.  Missing indices stay empty. */
bool messages_load(MessageTable *t, const char *path, char *err, size_t errsz);
void messages_free(MessageTable *t);
/* Entry i, or NULL when out of range or not loaded. */
const char *messages_get(const MessageTable *t, int index);

#endif
