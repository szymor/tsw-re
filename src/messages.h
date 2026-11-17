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

/* The language of the game text: a folder of data/text/, chosen in the
 * Options menu (options.ini `language`); English by default. */
#define MESSAGES_DEFAULT_LANG "en"
#define MESSAGES_LANG_LEN 16

const char *messages_lang(void);
/* Switch the folder messages_path reads.  False (and no change) for a
 * code that is not 1..15 of [a-z0-9_-]. */
bool messages_set_lang(const char *code);
/* The "language.name" entry of data/text/<code>/interface.tsv into
 * `out` ("Polski"); the code itself when there is none. */
void messages_lang_name(const char *data_dir, const char *code, char *out,
                        size_t outsz);

typedef struct {
    char **text;
    char **speaker;             /* the optional middle column; NULL = none */
    int count;
} MessageTable;

/* "<data_dir>/text/<lang>/<file>"; false when it does not fit. */
bool messages_path(char *out, size_t outsz, const char *data_dir,
                   const char *file);
/* Load "<index>\t<escaped text>" or "<index>\t<speaker>\t<escaped text>"
 * lines (a raw tab in the text is always escaped, so a second tab marks
 * the speaker column).  Missing indices stay empty. */
bool messages_load(MessageTable *t, const char *path, char *err, size_t errsz);
void messages_free(MessageTable *t);
/* Entry i, or NULL when out of range or not loaded. */
const char *messages_get(const MessageTable *t, int index);
/* Entry i's speaker id (speakers.h), or NULL when it has none. */
const char *messages_speaker(const MessageTable *t, int index);

/* The dialogue speakers: the id the speaker column of messages.tsv uses,
 * the English name (speakers.tsv translates it) and the icon, the two
 * Chtable frames the speaker's map tile alternates between (imagework's
 * frame B; frame_a < 0: the hero, from Player.png). */
typedef struct {
    const char *id;
    const char *name;
    int frame_a, frame_b;
} SpeakerDef;

#define SPEAKER_NAME_LEN 64
#define SPEAKER_MAX 16

typedef struct {
    char name[SPEAKER_MAX][SPEAKER_NAME_LEN];
} SpeakerNames;

int speaker_count(void);
const SpeakerDef *speaker_def(int speaker);
/* The speaker with this id, or -1 (NULL, empty or unknown). */
int speaker_find(const char *id);
/* The English names, then "<id>\t<name>" lines from `path` over them;
 * false when the file is missing or malformed (the names stay usable). */
void speaker_names_init(SpeakerNames *n);
bool speaker_names_load(SpeakerNames *n, const char *path);
const char *speaker_name(const SpeakerNames *n, int speaker);

#endif
