#include "items.h"

#include "map.h"
#include "state.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>

static const char *const SPECIAL_NAMES[SPECIAL_SLOT_COUNT] = {
    "Orb of the Hero", "Orb of Wisdom", "Orb of Flying", "Cross",
    "Magic Elixir", "Magic Mattock", "Bomb", "Warp Staff",
    "Wings up", "Wings down", "Dragon Slayer", "Snow Crystal",
    "Magic Key", "Super Mattock", "Lucky Gold"
};

const char *special_item_name(int slot)
{
    if (slot < 0 || slot >= SPECIAL_SLOT_COUNT)
        return "";
    return SPECIAL_NAMES[slot];
}

int special_item_icon(int slot)
{
    /* itemdisp() draws special slot s from atlas sprite 0x23 + s. */
    return 0x23 + slot;
}

const char *sword_name(int level)
{
    switch (level) {
    case 1: return "Iron Sword";
    case 2: return "Silver Sword";
    case 3: return "Knight Sword";
    case 4: return "Holy Sword";
    case 5: return "Sacred Sword";
    default: return "-";
    }
}

const char *shield_name(int level)
{
    switch (level) {
    case 1: return "Iron Shield";
    case 2: return "Silver Shield";
    case 3: return "Knight Shield";
    case 4: return "Holy Shield";
    case 5: return "Sacred Shield";
    default: return "-";
    }
}

static void add_state_clamped(Game *game, int index, int64_t delta)
{
    int64_t value = (int64_t)state_get(&game->state, index) + delta;
    if (value > INT32_MAX) value = INT32_MAX;
    if (value < INT32_MIN) value = INT32_MIN;
    state_set(&game->state, index, (int32_t)value);
}

static bool is_wall(uint8_t raw)
{
    return raw >= 0x07 && raw <= 0x0a;
}

static bool is_monster(uint8_t raw)
{
    return raw >= 0x3d && raw <= 0x5a;
}

static int adjacent_tiles(const Game *game, int *indices)
{
    static const int dx[4] = { 0, -1, 1, 0 };
    static const int dy[4] = { 1, 0, 0, -1 };
    int count = 0;
    int floor = game_floor(game);
    int x = game_x(game);
    int y = game_y(game);
    for (int i = 0; i < 4; i++) {
        int nx = x + dx[i];
        int ny = y + dy[i];
        if (nx < 0 || nx >= MAP_TILE_COLS ||
            ny < 0 || ny >= MAP_TILE_ROWS)
            continue;
        indices[count++] = ny * MAP_TILE_COLS + nx;
    }
    (void)floor;
    return count;
}

static bool travel_one_floor(Game *game, bool up, char *msg, size_t msgsz)
{
    int old_floor = game_floor(game);
    int new_floor = up ? (old_floor == 43 ? 45 : old_floor + 1)
                       : (old_floor == 45 ? 43 : old_floor - 1);
    if (new_floor < 1 || new_floor > 50)
        return false;
    int arrival = up ? map_stair_up(&game->map, new_floor)
                     : map_stair_down(&game->map, new_floor);
    if (arrival < 0 || arrival >= MAP_TILES)
        return false;

    state_set(&game->state, ST_FLOOR, new_floor);
    state_set(&game->state, ST_X, arrival % MAP_TILE_COLS);
    state_set(&game->state, ST_Y, arrival / MAP_TILE_COLS);
    if (new_floor > state_get(&game->state, ST_FLOOR_LIMIT))
        state_set(&game->state, ST_FLOOR_LIMIT, new_floor);
    if (msg && msgsz)
        snprintf(msg, msgsz, "Flew %s to floor %d.", up ? "up" : "down", new_floor);
    return true;
}

ItemUseResult item_use_special(Game *game, int slot, char *msg, size_t msgsz)
{
    if (!game || !game->has_state || slot < 0 || slot >= SPECIAL_SLOT_COUNT)
        return ITEM_USE_EMPTY;
    if (state_get(&game->state, ST_LIFE) <= 0) {
        if (msg && msgsz) snprintf(msg, msgsz, "You are dead.");
        return ITEM_USE_FAILED;
    }
    int value = state_get(&game->state, ST_SPECIAL_FIRST + slot);
    if (value == 0)
        return ITEM_USE_EMPTY;

    int floor = game_floor(game);
    switch (slot) {
    case 0:
        return ITEM_USE_FORECAST;
    case 1:
        if (msg && msgsz) snprintf(msg, msgsz, "The Orb of Wisdom's lore window is not implemented yet.");
        return ITEM_USE_PASSIVE;
    case 2:
        if (msg && msgsz) snprintf(msg, msgsz, "The Orb of Flying's travel UI is not implemented yet.");
        return ITEM_USE_PASSIVE;
    case 3:
        if (msg && msgsz) snprintf(msg, msgsz, "The Cross is passive and doubles damage against undead.");
        return ITEM_USE_PASSIVE;
    case 4: {
        int64_t gain = ((int64_t)state_get(&game->state, ST_ATTACK) +
                        state_get(&game->state, ST_DEFENSE)) * 10;
        add_state_clamped(game, ST_LIFE, gain);
        state_set(&game->state, ST_SPECIAL_FIRST + slot, 0);
        if (msg && msgsz) snprintf(msg, msgsz, "Used the Magic Elixir. Life +%lld.", (long long)gain);
        break;
    }
    case 5: {
        int indices[4];
        int count = adjacent_tiles(game, indices);
        int broken = 0;
        for (int i = 0; i < count; i++) {
            int off = map_floor_offset(floor) + indices[i];
            if (is_wall(game->map.bytes[off])) {
                game->map.bytes[off] = 0x06;
                broken++;
            }
        }
        if (msg && msgsz)
            snprintf(msg, msgsz, "Used the Magic Mattock. %d wall(s) fell down.", broken);
        break;
    }
    case 6: {
        int indices[4];
        int count = adjacent_tiles(game, indices);
        int killed = 0;
        for (int i = 0; i < count; i++) {
            int off = map_floor_offset(floor) + indices[i];
            if (is_monster(game->map.bytes[off])) {
                game->map.bytes[off] = 0x06;
                killed++;
            }
        }
        state_set(&game->state, ST_SPECIAL_FIRST + slot, 0);
        if (msg && msgsz)
            snprintf(msg, msgsz, "Used the Bomb. %d adjacent enemy/enemies died.", killed);
        break;
    }
    case 7: {
        int off = map_floor_offset(floor);
        int stair = -1;
        for (int i = 0; i < MAP_TILES; i++) {
            if (game->map.bytes[off + i] == 0x0b) {
                stair = i;
                break;
            }
        }
        if (stair < 0) {
            if (msg && msgsz) snprintf(msg, msgsz, "You cannot warp (There's an object in the way).");
            return ITEM_USE_FAILED;
        }
        state_set(&game->state, ST_X, stair % MAP_TILE_COLS);
        state_set(&game->state, ST_Y, stair / MAP_TILE_COLS);
        if (value > 1)
            state_set(&game->state, ST_SPECIAL_FIRST + slot, value - 1);
        else
            state_set(&game->state, ST_SPECIAL_FIRST + slot, 0);
        if (msg && msgsz) snprintf(msg, msgsz, "You've warped to the up stairs.");
        break;
    }
    case 8:
        if (!travel_one_floor(game, true, msg, msgsz)) {
            if (msg && msgsz) snprintf(msg, msgsz, "Cannot fly up from this floor.");
            return ITEM_USE_FAILED;
        }
        state_set(&game->state, ST_SPECIAL_FIRST + slot, 0);
        break;
    case 9:
        if (!travel_one_floor(game, false, msg, msgsz)) {
            if (msg && msgsz) snprintf(msg, msgsz, "Cannot fly down from this floor.");
            return ITEM_USE_FAILED;
        }
        state_set(&game->state, ST_SPECIAL_FIRST + slot, 0);
        break;
    case 10:
        if (msg && msgsz) snprintf(msg, msgsz, "The Dragon Slayer is passive and doubles damage against dragons.");
        return ITEM_USE_PASSIVE;
    case 11: {
        int off = map_floor_offset(floor);
        int changed = 0;
        for (int i = 0; i < MAP_TILES; i++) {
            if (game->map.bytes[off + i] == 0x0d) {
                game->map.bytes[off + i] = 0x0e;
                changed++;
            }
        }
        if (msg && msgsz) snprintf(msg, msgsz, "Used the Snow Crystal. %d lava tile(s) froze.", changed);
        break;
    }
    case 12: {
        int off = map_floor_offset(floor);
        int opened = 0;
        for (int i = 0; i < MAP_TILES; i++) {
            uint8_t raw = game->map.bytes[off + i];
            if (raw >= 0x01 && raw <= 0x03) {
                game->map.bytes[off + i] = 0x06;
                opened++;
            }
        }
        state_set(&game->state, ST_SPECIAL_FIRST + slot, 0);
        if (msg && msgsz) snprintf(msg, msgsz, "Used the Magic Key. %d door(s) opened.", opened);
        break;
    }
    case 13: {
        int off = map_floor_offset(floor);
        int broken = 0;
        for (int i = 0; i < MAP_TILES; i++) {
            if (is_wall(game->map.bytes[off + i])) {
                game->map.bytes[off + i] = 0x06;
                broken++;
            }
        }
        state_set(&game->state, ST_SPECIAL_FIRST + slot, 0);
        if (msg && msgsz) snprintf(msg, msgsz, "Used the Super Mattock. %d wall(s) fell down.", broken);
        break;
    }
    case 14:
        if (msg && msgsz) snprintf(msg, msgsz, "The Lucky Gold is passive and doubles battle gold.");
        return ITEM_USE_PASSIVE;
    default:
        return ITEM_USE_PASSIVE;
    }

    state_compute_checksums(&game->state);
    return ITEM_USE_OK;
}
