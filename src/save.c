#include "save.h"

#include <stdio.h>
#include <string.h>

static bool is_checkpoint_name(const char *path)
{
    const char *base = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\')
            base = p + 1;
    }
    return strcmp(base, "save0.dat") == 0;
}

bool save_parse_bytes_profile(const uint8_t *bytes, size_t len, SaveData *out,
                              bool allow_zero_checksums,
                              char *err, size_t errsz)
{
    if (!bytes || !out) {
        if (err && errsz) snprintf(err, errsz, "null save buffer");
        return false;
    }
    if (len < SAVE_FILE_SIZE) {
        if (err && errsz)
            snprintf(err, errsz, "save too short: %zu < %d",
                     len, SAVE_FILE_SIZE);
        return false;
    }
    if (!map_parse(bytes, MAP_BLOCK_SIZE, &out->map, err, errsz))
        return false;
    if (!state_parse(bytes + MAP_BLOCK_SIZE, STATE_BLOCK_SIZE,
                     &out->state, err, errsz))
        return false;
    out->status = state_validate_profile(&out->state, allow_zero_checksums,
                                         err, errsz);
    return out->status == STATE_OK || out->status == STATE_OK_CHECKPOINT;
}

bool save_parse_bytes(const uint8_t *bytes, size_t len, SaveData *out,
                      char *err, size_t errsz)
{
    return save_parse_bytes_profile(bytes, len, out, false, err, errsz);
}

bool save_load_file_profile(const char *path, SaveData *out,
                            bool allow_zero_checksums,
                            char *err, size_t errsz)
{
    if (!path || !out) {
        if (err && errsz) snprintf(err, errsz, "null save path");
        return false;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (err && errsz) snprintf(err, errsz, "cannot open %s", path);
        return false;
    }
    uint8_t buf[SAVE_FILE_SIZE + 1];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    return save_parse_bytes_profile(buf, n, out, allow_zero_checksums,
                                    err, errsz);
}

bool save_load_file(const char *path, SaveData *out, char *err, size_t errsz)
{
    return save_load_file_profile(path, out,
                                  path ? is_checkpoint_name(path) : false,
                                  err, errsz);
}
