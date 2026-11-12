#ifndef TSW_STATE_H
#define TSW_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STATE_INT_COUNT 171
#define STATE_BLOCK_SIZE (STATE_INT_COUNT * 4)

/* State-block indices (offset / 4). */
enum {
    ST_LIFE = 0x000 / 4,
    ST_ATTACK = 0x004 / 4,
    ST_DEFENSE = 0x008 / 4,
    ST_GOLD = 0x00c / 4,
    ST_FLOOR = 0x010 / 4,
    ST_FLOOR_LIMIT = 0x014 / 4,
    ST_X = 0x018 / 4,
    ST_Y = 0x01c / 4,
    ST_KEY_YELLOW = 0x020 / 4,
    ST_KEY_RED = 0x024 / 4,
    ST_KEY_BLUE = 0x028 / 4,
    ST_WEAPON_LEVEL = 0x03c / 4,
    ST_SHIELD_LEVEL = 0x040 / 4,
    ST_SPECIAL_FIRST = 0x044 / 4,
    ST_SPECIAL_LAST = 0x07c / 4,
    ST_SWORD_FLAG_FIRST = 0x080 / 4,
    ST_SHIELD_FLAG_FIRST = 0x094 / 4,
    /* Old-man flags used by roujin (0x449708).  F2 is the floor-2 one-shot
     * at 0x62 (0x4b8810).  roujin also writes state[event_selector] at
     * 0x449cc0 (1, or 2 with the Orb of Wisdom): 0x42 on floor 2 and the
     * talked-to flags 0x43/0x44/0x45 on floors 3/4/6 (15-event-flags.md
     * section 5). */
    ST_EVENT_FLAG_OLD_MAN_F2 = 0x62,
    ST_EVENT_FLAG_OLD_MAN_F3 = 0x43,
    ST_EVENT_FLAG_OLD_MAN_F4 = 0x44,
    ST_EVENT_FLAG_OLD_MAN_F6 = 0x45,
    /* Floor-10 area-1 completion flag.  The handler guard at 0x46cbc7 checks
     * 0x4b8830, i.e. state index 0x6a. */
    ST_EVENT_FLAG_FLOOR10 = 0x6a,
    ST_FACING = 0x160 / 4,
    ST_MAGIC = 0x274 / 4,
    ST_OPTION_A = 0x27c / 4,
    ST_OPTION_B = 0x280 / 4,
    ST_CHECKSUM_SUM = 0x290 / 4,
    ST_CHECKSUM_ALT = 0x294 / 4,
    ST_VERSION = 0x2a8 / 4
};

#define STATE_MAGIC_VALUE (-4)
#define STATE_VERSION_VALUE 12

typedef struct {
    int32_t raw[STATE_INT_COUNT];
} StateBlock;

typedef enum {
    STATE_OK = 0,
    STATE_OK_CHECKPOINT = 1,
    STATE_ERR_SIZE = -1,
    STATE_ERR_MAGIC = -2,
    STATE_ERR_VERSION = -3,
    STATE_ERR_CHECKSUM = -4
} StateStatus;

/* Parse/serialize 684 bytes of little-endian state data. */
bool state_parse(const uint8_t *bytes, size_t len, StateBlock *out,
                 char *err, size_t errsz);
bool state_serialize(const StateBlock *state, uint8_t *bytes, size_t len);

/* Compute and store the sum/alternating checksums at the documented indices. */
void state_compute_checksums(StateBlock *state);

/* Validate magic, version and checksums.  Normal saves require both stored
 * checksums to match.  The checkpoint profile (save0.dat) additionally accepts
 * the all-zero checksum pair and reports STATE_OK_CHECKPOINT. */
StateStatus state_validate(const StateBlock *state, char *err, size_t errsz);
StateStatus state_validate_profile(const StateBlock *state,
                                   bool allow_zero_checksums,
                                   char *err, size_t errsz);

int32_t state_get(const StateBlock *state, int index);
void state_set(StateBlock *state, int index, int32_t value);
const char *state_status_string(StateStatus status);

#endif
