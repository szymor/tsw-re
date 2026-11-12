#include "state.h"

#include <stdio.h>
#include <string.h>

static int32_t read_i32le(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] |
                     ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) |
                     ((uint32_t)p[3] << 24));
}

static void write_i32le(uint8_t *p, int32_t v)
{
    uint32_t u = (uint32_t)v;
    p[0] = (uint8_t)(u & 0xff);
    p[1] = (uint8_t)((u >> 8) & 0xff);
    p[2] = (uint8_t)((u >> 16) & 0xff);
    p[3] = (uint8_t)((u >> 24) & 0xff);
}

bool state_parse(const uint8_t *bytes, size_t len, StateBlock *out,
                 char *err, size_t errsz)
{
    if (!bytes || !out) {
        if (err && errsz) snprintf(err, errsz, "null state buffer");
        return false;
    }
    if (len < STATE_BLOCK_SIZE) {
        if (err && errsz)
            snprintf(err, errsz, "state block too short: %zu < %d",
                     len, STATE_BLOCK_SIZE);
        return false;
    }
    for (int i = 0; i < STATE_INT_COUNT; i++)
        out->raw[i] = read_i32le(bytes + i * 4);
    return true;
}

bool state_serialize(const StateBlock *state, uint8_t *bytes, size_t len)
{
    if (!state || !bytes || len < STATE_BLOCK_SIZE)
        return false;
    for (int i = 0; i < STATE_INT_COUNT; i++)
        write_i32le(bytes + i * 4, state->raw[i]);
    return true;
}

void state_compute_checksums(StateBlock *state)
{
    if (!state)
        return;
    int32_t sum = 0;
    int32_t alt = 0;
    for (int i = 0; i < 164; i++) {
        int32_t v = state->raw[i];
        sum = (int32_t)((uint32_t)sum + (uint32_t)v);
        if (i & 1)
            alt = (int32_t)((uint32_t)alt - (uint32_t)v);
        else
            alt = (int32_t)((uint32_t)alt + (uint32_t)v);
    }
    state->raw[ST_CHECKSUM_SUM] = sum;
    state->raw[ST_CHECKSUM_ALT] = alt;
}

StateStatus state_validate_profile(const StateBlock *state,
                                   bool allow_zero_checksums,
                                   char *err, size_t errsz)
{
    if (!state) {
        if (err && errsz) snprintf(err, errsz, "null state");
        return STATE_ERR_SIZE;
    }
    if (state->raw[ST_MAGIC] != STATE_MAGIC_VALUE) {
        if (err && errsz)
            snprintf(err, errsz, "bad save magic: %d (expected %d)",
                     state->raw[ST_MAGIC], STATE_MAGIC_VALUE);
        return STATE_ERR_MAGIC;
    }
    if (state->raw[ST_VERSION] != STATE_VERSION_VALUE) {
        if (err && errsz)
            snprintf(err, errsz, "bad save version: %d (expected %d)",
                     state->raw[ST_VERSION], STATE_VERSION_VALUE);
        return STATE_ERR_VERSION;
    }

    int32_t stored_sum = state->raw[ST_CHECKSUM_SUM];
    int32_t stored_alt = state->raw[ST_CHECKSUM_ALT];
    if (stored_sum == 0 && stored_alt == 0 && allow_zero_checksums)
        return STATE_OK_CHECKPOINT;

    StateBlock copy = *state;
    state_compute_checksums(&copy);
    if (copy.raw[ST_CHECKSUM_SUM] != stored_sum ||
        copy.raw[ST_CHECKSUM_ALT] != stored_alt) {
        if (err && errsz) {
            if (stored_sum == 0 && stored_alt == 0)
                snprintf(err, errsz,
                         "zero checksums do not match computed %d/%d; "
                         "they are only accepted for the save0 checkpoint",
                         copy.raw[ST_CHECKSUM_SUM],
                         copy.raw[ST_CHECKSUM_ALT]);
            else
                snprintf(err, errsz,
                         "checksum mismatch: stored %d/%d, computed %d/%d",
                         stored_sum, stored_alt,
                         copy.raw[ST_CHECKSUM_SUM],
                         copy.raw[ST_CHECKSUM_ALT]);
        }
        return STATE_ERR_CHECKSUM;
    }
    return STATE_OK;
}

StateStatus state_validate(const StateBlock *state, char *err, size_t errsz)
{
    return state_validate_profile(state, false, err, errsz);
}

void state_syokidata(StateBlock *state)
{
    if (!state) return;
    int32_t *r = state->raw;
    bool keep_stats = r[ST_OPTION_A] != 0;     /* opt_difficulty 0x4b8904 */
    bool carry = r[ST_OPTION_B] != 0;          /* opt_carry 0x4b8908 */
    if (!keep_stats) {
        r[ST_LIFE] = 1000;
        r[ST_ATTACK] = 100;
        r[ST_DEFENSE] = 100;
    }
    if (!carry) {
        r[ST_GOLD] = 0;
        r[ST_FLOOR_LIMIT] = 1;
    }
    r[ST_FLOOR] = 1;
    r[ST_X] = 5;
    r[ST_Y] = 10;
    for (int i = ST_KEY_YELLOW; i <= 0x0e; i++)  /* keys, 0x0b..0x0e */
        r[i] = 0;
    r[ST_WEAPON_LEVEL] = 5;
    r[ST_SHIELD_LEVEL] = 5;
    if (!carry) {
        /* Items and swords/shields; the Sacred pair (slot 4) is held. */
        for (int i = ST_SPECIAL_FIRST; i <= ST_SHIELD_FLAG_FIRST + 4; i++)
            r[i] = 0;
        r[ST_SWORD_FLAG_FIRST + 4] = 1;
        r[ST_SHIELD_FLAG_FIRST + 4] = 1;
    }
    for (int i = 0x2a; i <= 0x57; i++)
        r[i] = 0;
    r[ST_FACING] = 4;
    for (int i = 0x5a; i <= 0x9c; i++)
        r[i] = 0;
    r[0x9e] = 0;
    for (int i = 0xa1; i <= 0xa9; i++)          /* 0xa4/0xa5: checksums */
        r[i] = 0;
}

int32_t state_get(const StateBlock *state, int index)
{
    if (!state || index < 0 || index >= STATE_INT_COUNT)
        return 0;
    return state->raw[index];
}

void state_set(StateBlock *state, int index, int32_t value)
{
    if (!state || index < 0 || index >= STATE_INT_COUNT)
        return;
    state->raw[index] = value;
}

const char *state_status_string(StateStatus status)
{
    switch (status) {
    case STATE_OK: return "ok";
    case STATE_OK_CHECKPOINT: return "ok (checkpoint / zero checksums)";
    case STATE_ERR_SIZE: return "size error";
    case STATE_ERR_MAGIC: return "bad magic";
    case STATE_ERR_VERSION: return "bad version";
    case STATE_ERR_CHECKSUM: return "bad checksum";
    default: return "unknown";
    }
}
