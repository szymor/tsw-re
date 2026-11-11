#include "map.h"
#include "save.h"
#include "state.h"

#include <stdio.h>
#include <string.h>

static void write_i32le(uint8_t *p, int32_t v)
{
    uint32_t u = (uint32_t)v;
    p[0] = (uint8_t)(u & 0xff);
    p[1] = (uint8_t)((u >> 8) & 0xff);
    p[2] = (uint8_t)((u >> 16) & 0xff);
    p[3] = (uint8_t)((u >> 24) & 0xff);
}

static void write_state(uint8_t *dst, const StateBlock *state)
{
    for (int i = 0; i < STATE_INT_COUNT; i++)
        write_i32le(dst + i * 4, state->raw[i]);
}

static void make_save(uint8_t *buf, StateBlock *state, bool valid_checksums)
{
    memset(buf, 0, SAVE_FILE_SIZE);
    buf[0] = 0x00;
    buf[1] = 0x01;
    int off = map_floor_offset(1);
    for (int i = 0; i < MAP_TILES; i++)
        buf[off + i] = 0x06;
    buf[off + 0] = 0x08;
    buf[off + 120] = 0x0b;
    buf[map_stair_up_offset(1)] = 0x0b;
    buf[map_stair_down_offset(1)] = 0x0c;

    memset(state, 0, sizeof(*state));
    state->raw[ST_LIFE] = 1000;
    state->raw[ST_ATTACK] = 100;
    state->raw[ST_DEFENSE] = 80;
    state->raw[ST_GOLD] = 42;
    state->raw[ST_FLOOR] = 1;
    state->raw[ST_X] = 5;
    state->raw[ST_Y] = 10;
    state->raw[ST_MAGIC] = STATE_MAGIC_VALUE;
    state->raw[ST_VERSION] = STATE_VERSION_VALUE;
    if (valid_checksums)
        state_compute_checksums(state);
    write_state(buf + MAP_BLOCK_SIZE, state);
}

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void)
{
    uint8_t buf[SAVE_FILE_SIZE];
    StateBlock state;
    char err[256];

    CHECK(map_floor_offset(1) == 125);
    CHECK(map_stair_up_offset(1) == 123);
    CHECK(map_stair_down_offset(1) == 124);
    CHECK(map_floor_offset(50) + MAP_TILES == MAP_BLOCK_SIZE);

    make_save(buf, &state, true);
    CHECK(state.raw[ST_CHECKSUM_SUM] == 1234);
    CHECK(state.raw[ST_CHECKSUM_ALT] == 938);

    StateBlock independent;
    memset(&independent, 0, sizeof(independent));
    independent.raw[ST_LIFE] = 1000;
    independent.raw[ST_ATTACK] = 100;
    independent.raw[ST_DEFENSE] = 80;
    independent.raw[ST_GOLD] = 42;
    independent.raw[ST_FLOOR] = 1;
    independent.raw[ST_X] = 5;
    independent.raw[ST_Y] = 10;
    independent.raw[ST_MAGIC] = STATE_MAGIC_VALUE;
    independent.raw[ST_VERSION] = STATE_VERSION_VALUE;
    independent.raw[ST_CHECKSUM_SUM] = 1234;
    independent.raw[ST_CHECKSUM_ALT] = 938;
    err[0] = '\0';
    CHECK(state_validate(&independent, err, sizeof(err)) == STATE_OK);

    /* A correctly checksummed normal state may legitimately wrap to 0/0. */
    StateBlock wrap;
    memset(&wrap, 0, sizeof(wrap));
    wrap.raw[ST_LIFE] = INT32_MAX;
    wrap.raw[ST_ATTACK] = 3;
    wrap.raw[ST_DEFENSE] = INT32_MAX;
    wrap.raw[ST_FLOOR] = 1;
    wrap.raw[ST_FLOOR_LIMIT] = 1;
    wrap.raw[ST_X] = 1;
    wrap.raw[ST_MAGIC] = STATE_MAGIC_VALUE;
    wrap.raw[ST_VERSION] = STATE_VERSION_VALUE;
    state_compute_checksums(&wrap);
    CHECK(wrap.raw[ST_CHECKSUM_SUM] == 0);
    CHECK(wrap.raw[ST_CHECKSUM_ALT] == 0);
    err[0] = '\0';
    CHECK(state_validate(&wrap, err, sizeof(err)) == STATE_OK);

    SaveData save;
    memset(&save, 0, sizeof(save));
    err[0] = '\0';
    CHECK(save_parse_bytes(buf, sizeof(buf), &save, err, sizeof(err)));
    CHECK(save.status == STATE_OK);
    CHECK(save.state.raw[ST_LIFE] == 1000);
    CHECK(save.state.raw[ST_FLOOR] == 1);
    CHECK(map_tile(&save.map, 1, 0) == 0x08);
    CHECK(map_tile(&save.map, 1, 120) == 0x0b);
    CHECK(map_stair_up(&save.map, 1) == 0x0b);
    CHECK(map_stair_down(&save.map, 1) == 0x0c);

    make_save(buf, &state, true);
    state.raw[ST_LIFE] = 999;
    write_state(buf + MAP_BLOCK_SIZE, &state);
    err[0] = '\0';
    CHECK(!save_parse_bytes(buf, sizeof(buf), &save, err, sizeof(err)));
    CHECK(save.status == STATE_ERR_CHECKSUM);
    CHECK(strstr(err, "checksum") != NULL);

    make_save(buf, &state, false);
    memset(&save, 0, sizeof(save));
    err[0] = '\0';
    CHECK(!save_parse_bytes(buf, sizeof(buf), &save, err, sizeof(err)));
    CHECK(save.status == STATE_ERR_CHECKSUM);
    CHECK(strstr(err, "zero") != NULL);

    memset(&save, 0, sizeof(save));
    err[0] = '\0';
    CHECK(save_parse_bytes_profile(buf, sizeof(buf), &save, true,
                                   err, sizeof(err)));
    CHECK(save.status == STATE_OK_CHECKPOINT);

    make_save(buf, &state, true);
    state.raw[ST_MAGIC] = 0;
    write_state(buf + MAP_BLOCK_SIZE, &state);
    err[0] = '\0';
    CHECK(!save_parse_bytes(buf, sizeof(buf), &save, err, sizeof(err)));
    CHECK(save.status == STATE_ERR_MAGIC);

    printf("test_state: ok\n");
    return 0;
}
