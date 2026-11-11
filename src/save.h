#ifndef TSW_SAVE_H
#define TSW_SAVE_H

#include "map.h"
#include "state.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SAVE_FILE_SIZE (MAP_BLOCK_SIZE + STATE_BLOCK_SIZE)

typedef struct {
    MapBlock map;
    StateBlock state;
    StateStatus status;
} SaveData;

/* Normal saves require valid checksums.  Pass allow_zero_checksums=true only
 * for the save0.dat checkpoint profile. */
bool save_parse_bytes_profile(const uint8_t *bytes, size_t len, SaveData *out,
                              bool allow_zero_checksums,
                              char *err, size_t errsz);
bool save_load_file_profile(const char *path, SaveData *out,
                            bool allow_zero_checksums,
                            char *err, size_t errsz);

bool save_parse_bytes(const uint8_t *bytes, size_t len, SaveData *out,
                      char *err, size_t errsz);
bool save_load_file(const char *path, SaveData *out, char *err, size_t errsz);

/* Write a normal save with freshly computed checksums. */
bool save_write_file(const SaveData *save, const char *path,
                     char *err, size_t errsz);

#endif
