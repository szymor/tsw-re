#ifndef TSW_MESSAGES_H
#define TSW_MESSAGES_H

/* The original dialogue list: TTSW10.ListBox2.Items (form field +0x44c),
 * extracted from the user's TSW.exe into data/text/messages.tsv by
 * tools/extract_messages.py.  Indexed exactly like the original
 * (docs/re/18-messages.md).  SDL-free. */

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char **text;
    int count;
} MessageTable;

/* Load "<index>\t<escaped text>" lines.  Missing indices stay empty. */
bool messages_load(MessageTable *t, const char *path, char *err, size_t errsz);
void messages_free(MessageTable *t);
/* Entry i, or NULL when out of range or not loaded. */
const char *messages_get(const MessageTable *t, int index);

#endif
