#include "save.h"

#include "fileio.h"

#include <stdio.h>
#include <stdlib.h>
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
    size_t n = 0;
    char *buf = file_load(path, &n);
    if (!buf) {
        if (err && errsz) snprintf(err, errsz, "cannot open %s", path);
        return false;
    }
    bool ok = save_parse_bytes_profile((const uint8_t *)buf, n, out,
                                       allow_zero_checksums, err, errsz);
    file_free(buf);
    return ok;
}

bool save_load_file(const char *path, SaveData *out, char *err, size_t errsz)
{
    return save_load_file_profile(path, out,
                                  path ? is_checkpoint_name(path) : false,
                                  err, errsz);
}

static bool write_save_file(const SaveData *save, const char *path,
                            bool checkpoint, char *err, size_t errsz)
{
    uint8_t buf[SAVE_FILE_SIZE];
    memcpy(buf, save->map.bytes, MAP_BLOCK_SIZE);
    StateBlock state = save->state;
    if (checkpoint) {
        state.raw[ST_CHECKSUM_SUM] = 0;
        state.raw[ST_CHECKSUM_ALT] = 0;
    } else {
        state_compute_checksums(&state);
    }
    if (!state_serialize(&state, buf + MAP_BLOCK_SIZE, STATE_BLOCK_SIZE)) {
        if (err && errsz) snprintf(err, errsz, "cannot serialize state");
        return false;
    }

    if (!file_save(path, buf, sizeof(buf))) {
        if (err && errsz) snprintf(err, errsz, "cannot write %s", path);
        return false;
    }
    return true;
}

bool save_write_file(const SaveData *save, const char *path,
                     char *err, size_t errsz)
{
    if (!save || !path) {
        if (err && errsz) snprintf(err, errsz, "null save/path");
        return false;
    }
    if (is_checkpoint_name(path)) {
        if (err && errsz)
            snprintf(err, errsz, "normal save path must not be save0.dat");
        return false;
    }
    return write_save_file(save, path, false, err, errsz);
}

bool save_write_checkpoint_file(const SaveData *save, const char *path,
                                char *err, size_t errsz)
{
    if (!save || !path) {
        if (err && errsz) snprintf(err, errsz, "null save/path");
        return false;
    }
    return write_save_file(save, path, true, err, errsz);
}
