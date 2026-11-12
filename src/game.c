#include "game.h"

#include "battle.h"
#include "items.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

void game_init(Game *game)
{
    if (!game) return;
    memset(game, 0, sizeof(*game));
}

void game_new(Game *game, const MapBlock *initial_map)
{
    if (!game) return;
    memset(game, 0, sizeof(*game));
    if (initial_map)
        game->map = *initial_map;

    state_set(&game->state, ST_LIFE, 1000);
    state_set(&game->state, ST_ATTACK, 100);
    state_set(&game->state, ST_DEFENSE, 100);
    state_set(&game->state, ST_GOLD, 0);
    state_set(&game->state, ST_FLOOR, 1);
    state_set(&game->state, ST_FLOOR_LIMIT, 1);
    state_set(&game->state, ST_X, 5);
    state_set(&game->state, ST_Y, 10);
    state_set(&game->state, ST_FACING, 4);
    state_set(&game->state, ST_MAGIC, STATE_MAGIC_VALUE);
    state_set(&game->state, ST_VERSION, STATE_VERSION_VALUE);
    state_compute_checksums(&game->state);
    game->has_state = true;
}

void game_load(Game *game, const SaveData *save)
{
    if (!game || !save) return;
    game->map = save->map;
    game->state = save->state;
    game->has_state = true;

    int floor = (int)state_get(&game->state, ST_FLOOR);
    if (floor < 1 || floor > 50)
        state_set(&game->state, ST_FLOOR, 1);
    int x = (int)state_get(&game->state, ST_X);
    int y = (int)state_get(&game->state, ST_Y);
    if (x < 0 || x >= MAP_TILE_COLS)
        state_set(&game->state, ST_X, 5);
    if (y < 0 || y >= MAP_TILE_ROWS)
        state_set(&game->state, ST_Y, 10);
    state_compute_checksums(&game->state);
}

void game_snapshot(const Game *game, SaveData *out)
{
    if (!game || !out) return;
    memset(out, 0, sizeof(*out));
    out->map = game->map;
    out->state = game->state;
    state_compute_checksums(&out->state);
    out->status = STATE_OK;
}

int game_floor(const Game *game)
{
    return game ? (int)state_get(&game->state, ST_FLOOR) : 0;
}

int game_x(const Game *game)
{
    return game ? (int)state_get(&game->state, ST_X) : 0;
}

int game_y(const Game *game)
{
    return game ? (int)state_get(&game->state, ST_Y) : 0;
}

int32_t game_stat(const Game *game, int index)
{
    return game ? state_get(&game->state, index) : 0;
}

static bool is_walkable(const Game *game, uint8_t raw)
{
    (void)game;
    switch (raw) {
    case 0x04: /* open door / frame */
    case 0x05:
    case 0x06: /* floor */
    case 0x0e: /* ice / frozen lava */
    case 0x0f:
    case 0x10:
    case 0x11:
        return true;
    default:
        return false;
    }
}

static bool is_monster(uint8_t raw)
{
    return raw >= 0x3d && raw <= 0x5a;
}

static bool is_item(uint8_t raw)
{
    return raw >= 0x20 && raw <= 0x3c;
}

static bool is_scripted_wall_event(int floor, int index, uint8_t raw)
{
    /* Floor 3 raw 0x07 at (4,8) is the opening Zeno trigger (handan ->
     * mevent case 0x463a2c), not a wall.  The event engine moves the
     * player onto the tile and clears it (event_engine.c). */
    if (floor == 3 && raw == 0x07)
        return true;
    /* Floor 10 raw 0x07 at (5,4): the boss-room trigger (mevent case 10 ->
     * idou, tile cleared, ichicheck floor 10). */
    if (floor == 10 && index == 49 && raw == 0x07)
        return true;
    /* The brown 0x09 brick is the press-to-collapse secret wall.  The
     * initial map uses it on floor 5 at (8,10), next to the Iron Sword,
     * and on floor 9 at (9,4). */
    if (raw == 0x09 && ((floor == 5 && index == 118) ||
                        (floor == 9 && index == 53)))
        return true;
    return false;
}

static bool is_event_tile(uint8_t raw)
{
    if (raw == 0x13 || raw == 0x15 || raw == 0x17 || raw == 0x19 || raw == 0x1b)
        return true;
    if (raw >= 0x5b && raw <= 0x5d)
        return true;
    if (raw >= 0x61 && raw <= 0x69)
        return true;
    if (raw >= 0x73 && raw <= 0x7b)
        return true;
    if (raw >= 0x85 && raw <= 0x9d)
        return true;
    return false;
}

static void move_to(Game *game, int x, int y)
{
    state_set(&game->state, ST_X, x);
    state_set(&game->state, ST_Y, y);
    state_compute_checksums(&game->state);
}

static GameMoveResult open_door(Game *game, int x, int y, int index,
                                uint8_t raw, char *msg, size_t msgsz)
{
    int key_index;
    const char *color;
    switch (raw) {
    case 0x01:
        key_index = ST_KEY_YELLOW;
        color = "yellow";
        break;
    case 0x02:
        key_index = ST_KEY_BLUE;
        color = "blue";
        break;
    default:
        key_index = ST_KEY_RED;
        color = "red";
        break;
    }

    int keys = state_get(&game->state, key_index);
    if (keys <= 0) {
        if (msg && msgsz)
            snprintf(msg, msgsz, "You don't have the %s key.", color);
        return GAME_MOVE_BLOCKED;
    }

    state_set(&game->state, key_index, keys - 1);
    game->map.bytes[map_floor_offset(game_floor(game)) + index] = 0x06;
    move_to(game, x, y);
    if (msg && msgsz)
        snprintf(msg, msgsz, "The %s door has opened.", color);
    return GAME_MOVE_DOOR_OPENED;
}

static GameMoveResult pick_up_key(Game *game, int x, int y, int index,
                                  uint8_t raw, char *msg, size_t msgsz)
{
    int key_index;
    const char *color;
    switch (raw) {
    case 0x1d:
        key_index = ST_KEY_YELLOW;
        color = "yellow";
        break;
    case 0x1e:
        key_index = ST_KEY_RED;
        color = "red";
        break;
    default:
        key_index = ST_KEY_BLUE;
        color = "blue";
        break;
    }
    int keys = state_get(&game->state, key_index);
    if (keys == INT32_MAX) {
        if (msg && msgsz)
            snprintf(msg, msgsz, "The %s key count is already full.", color);
        return GAME_MOVE_BLOCKED;
    }
    state_set(&game->state, key_index, keys + 1);
    game->map.bytes[map_floor_offset(game_floor(game)) + index] = 0x06;
    move_to(game, x, y);
    if (msg && msgsz)
        snprintf(msg, msgsz, "Got a %s key.", color);
    return GAME_MOVE_KEY_PICKUP;
}

static GameMoveResult use_stairs(Game *game, uint8_t raw,
                                 char *msg, size_t msgsz)
{
    int old_floor = game_floor(game);
    bool up = raw == 0x0b;
    int new_floor;
    if (up)
        new_floor = (old_floor == 43) ? 45 : old_floor + 1;
    else
        new_floor = (old_floor == 45) ? 43 : old_floor - 1;

    if (new_floor < 1 || new_floor > 50) {
        if (msg && msgsz)
            snprintf(msg, msgsz, "The stairs lead nowhere.");
        return GAME_MOVE_BLOCKED;
    }

    int arrival = up ? map_stair_up(&game->map, new_floor)
                     : map_stair_down(&game->map, new_floor);
    if (arrival < 0 || arrival >= MAP_TILES) {
        if (msg && msgsz)
            snprintf(msg, msgsz, "The stair arrival tile is invalid.");
        return GAME_MOVE_BLOCKED;
    }

    state_set(&game->state, ST_FLOOR, new_floor);
    state_set(&game->state, ST_X, arrival % MAP_TILE_COLS);
    state_set(&game->state, ST_Y, arrival / MAP_TILE_COLS);
    if (new_floor > state_get(&game->state, ST_FLOOR_LIMIT))
        state_set(&game->state, ST_FLOOR_LIMIT, new_floor);
    state_compute_checksums(&game->state);

    if (msg && msgsz)
        snprintf(msg, msgsz, "Went %s the stairs to floor %d.",
                 up ? "up" : "down", new_floor);
    return up ? GAME_MOVE_STAIRS_UP : GAME_MOVE_STAIRS_DOWN;
}

GameMoveResult game_fight(Game *game, int monster_index,
                          char *msg, size_t msgsz)
{
    if (msg && msgsz)
        msg[0] = '\0';
    if (!game || !game->has_state ||
        state_get(&game->state, ST_LIFE) <= 0)
        return GAME_MOVE_DEAD;
    if (monster_index < 0 || monster_index >= MONSTER_COUNT) {
        if (msg && msgsz)
            snprintf(msg, msgsz, "Invalid monster.");
        return GAME_MOVE_MONSTER;
    }

    BattleResult br = battle_simulate(game, monster_index);
    if (!br.can_win) {
        if (msg && msgsz)
            snprintf(msg, msgsz, "You cannot defeat the %s yet.",
                     MONSTER_TABLE[monster_index].name);
        return GAME_MOVE_MONSTER;
    }
    int life = state_get(&game->state, ST_LIFE) - br.damage_to_player;
    if (life <= 0) {
        state_set(&game->state, ST_LIFE, 0);
        state_compute_checksums(&game->state);
        if (msg && msgsz)
            snprintf(msg, msgsz, "You were killed by the %s.",
                     MONSTER_TABLE[monster_index].name);
        return GAME_MOVE_DEAD;
    }
    state_set(&game->state, ST_LIFE, life);
    int64_t gold = (int64_t)state_get(&game->state, ST_GOLD) + br.reward_gold;
    if (gold > INT32_MAX) gold = INT32_MAX;
    if (gold < INT32_MIN) gold = INT32_MIN;
    state_set(&game->state, ST_GOLD, (int32_t)gold);
    state_compute_checksums(&game->state);
    if (msg && msgsz)
        snprintf(msg, msgsz, "Defeated the %s! Life -%d, gold +%d.",
                 MONSTER_TABLE[monster_index].name,
                 br.damage_to_player, br.reward_gold);
    return GAME_MOVE_BATTLE_WIN;
}

GameMoveResult game_move(Game *game, int dx, int dy,
                         char *msg, size_t msgsz)
{
    if (msg && msgsz)
        msg[0] = '\0';
    if (!game || !game->has_state)
        return GAME_MOVE_BLOCKED;
    if (state_get(&game->state, ST_LIFE) <= 0) {
        if (msg && msgsz)
            snprintf(msg, msgsz, "You are dead.");
        return GAME_MOVE_DEAD;
    }

    int facing = 0;
    if (dy > 0) facing = 1;       /* down */
    else if (dx < 0) facing = 2;  /* left */
    else if (dx > 0) facing = 3;  /* right */
    else if (dy < 0) facing = 4;  /* up */
    if (facing != 0) {
        state_set(&game->state, ST_FACING, facing);
        state_compute_checksums(&game->state);
    }

    int x = game_x(game);
    int y = game_y(game);
    int nx = x + dx;
    int ny = y + dy;
    if (nx < 0 || nx >= MAP_TILE_COLS ||
        ny < 0 || ny >= MAP_TILE_ROWS) {
        if (msg && msgsz)
            snprintf(msg, msgsz, "The edge of the floor blocks the way.");
        return GAME_MOVE_BLOCKED;
    }

    int floor = game_floor(game);
    if (floor < 1 || floor > 50) {
        if (msg && msgsz)
            snprintf(msg, msgsz, "The current floor is invalid.");
        return GAME_MOVE_BLOCKED;
    }
    int index = ny * MAP_TILE_COLS + nx;
    uint8_t raw = map_tile(&game->map, floor, index);

    if (raw == 0x01 || raw == 0x02 || raw == 0x03)
        return open_door(game, nx, ny, index, raw, msg, msgsz);
    if (raw == 0x1d || raw == 0x1e || raw == 0x1f)
        return pick_up_key(game, nx, ny, index, raw, msg, msgsz);
    if (raw == 0x0b || raw == 0x0c)
        return use_stairs(game, raw, msg, msgsz);

    if (is_scripted_wall_event(floor, index, raw)) {
        game->event_floor = floor;
        game->event_index = index;
        game->event_raw = raw;
        if (msg && msgsz)
            snprintf(msg, msgsz, "The wall looks different.");
        return GAME_MOVE_EVENT;
    }

    if (is_walkable(game, raw)) {
        move_to(game, nx, ny);
        return GAME_MOVE_OK;
    }

    if (raw == 0x0d) {
        if (msg && msgsz)
            snprintf(msg, msgsz, "The lava is too hot to cross.");
        return GAME_MOVE_LAVA_BLOCKED;
    }
    if (is_monster(raw)) {
        GameMoveResult result = game_fight(game, monster_index_from_raw(raw),
                                           msg, msgsz);
        if (result == GAME_MOVE_BATTLE_WIN) {
            game->map.bytes[map_floor_offset(floor) + index] = 0x06;
            move_to(game, nx, ny);
        }
        return result;
    }
    if (is_item(raw)) {
        char item_msg[256] = "";
        if (item_apply_pickup(game, raw, item_msg, sizeof(item_msg))) {
            game->map.bytes[map_floor_offset(floor) + index] = 0x06;
            move_to(game, nx, ny);
            if (msg && msgsz)
                snprintf(msg, msgsz, "%s", item_msg);
            return GAME_MOVE_ITEM_PICKUP;
        }
        if (msg && msgsz)
            snprintf(msg, msgsz, "Item pickup is not implemented yet.");
        return GAME_MOVE_ITEM;
    }
    if (is_event_tile(raw)) {
        game->event_floor = floor;
        game->event_index = index;
        game->event_raw = raw;
        if (msg && msgsz)
            snprintf(msg, msgsz, "An event is here.");
        return GAME_MOVE_EVENT;
    }

    if (msg && msgsz)
        snprintf(msg, msgsz, "The way is blocked.");
    return GAME_MOVE_BLOCKED;
}
