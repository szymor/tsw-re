#ifndef TSW_FILEIO_H
#define TSW_FILEIO_H

/* Files and folders through SDL3, the same code on every platform: paths
 * are UTF-8 (on Windows too), and a write replaces its file in one step.
 * Callers read a whole file and parse it from memory. */

#include <stdbool.h>
#include <stddef.h>

/* The whole file, NUL-terminated (free with file_free), or NULL when it
 * cannot be read.  `*len` (optional) gets its size without the NUL. */
char *file_load(const char *path, size_t *len);
void file_free(void *data);

/* Writes `len` bytes to `path` through a new temporary file beside it,
 * then renames that over `path`: a failed write leaves the old file whole,
 * and an existing file of the temporary's name is never touched. */
bool file_save(const char *path, const void *data, size_t len);

/* Creates the folder that holds `path`, and any above it. */
bool path_make_parent(const char *path);

bool path_is_file(const char *path);
bool path_is_dir(const char *path);

/* The next line of a loaded text, as fgets reads one: at most `size - 1`
 * bytes, up to and including '\n'.  `*cursor` starts at the text and
 * advances; false at `end`. */
bool text_next_line(const char **cursor, const char *end, char *line,
                    size_t size);

/* The executable's folder, without a trailing separator. */
bool path_exe_dir(char *out, size_t outsz);

/* The player's folder, for the saves and options.ini, created if needed:
 * $XDG_DATA_HOME/tsw (else ~/.local/share/tsw) on Linux, %APPDATA%\tsw on
 * Windows.  No trailing separator. */
bool path_user_dir(char *out, size_t outsz);

#endif
