#ifndef TSW_OPTIONS_H
#define TSW_OPTIONS_H

/* The player's options (the original's Options menu), kept in
 * options.ini in the player's folder (path_user_dir: beside the saves) as
 * `key = value` lines and written on every change. */

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    OPT_TEXT_TINY = 0,
    OPT_TEXT_SMALL,
    OPT_TEXT_NORMAL,
    OPT_TEXT_BIG,
    OPT_TEXT_COUNT
} OptTextSize;

typedef struct {
    bool sound;          /* Wav Sound On (state[0x59] = 0) */
    bool music;          /* Background Music On (state[0x5a] != 0) */
    int speed;           /* EVM_SPEED_HIGH / MIDDLE / LOW */
    OptTextSize text;    /* the message text size (the Font menu) */
    char language[16];   /* data/text/<language>/ (messages.h) */
} Options;

/* The defaults: sound and music on, middle speed, normal text. */
void options_defaults(Options *o);
/* The options file's path into out; false if no home directory. */
bool options_path(char *out, size_t outsz);
/* Read `path` over the defaults; unknown keys and bad values are skipped.
 * False if the file could not be read (o keeps the defaults). */
bool options_load(Options *o, const char *path);
/* Write `path`, creating its directory. */
bool options_save(const Options *o, const char *path);
/* Labels for the menu. */
const char *options_speed_label(int speed);
const char *options_text_label(OptTextSize text);

#endif
