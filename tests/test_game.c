#include "game.h"
#include "map.h"
#include "save.h"
#include "state.h"

#include <stdio.h>
#include <string.h>

static void make_map(MapBlock *map)
{
    memset(map, 0, sizeof(*map));
    map->bytes[0] = 0x00;
    map->bytes[1] = 0x01;
    for (int floor = 1; floor < MAP_FLOOR_COUNT; floor++) {
        int off = map_floor_offset(floor);
        memset(map->bytes + off, 0x06, MAP_TILES);
        map->bytes[map_stair_up_offset(floor)] = 0;
        map->bytes[map_stair_down_offset(floor)] = 0;
    }
}

static void set_tile(Game *game, int floor, int x, int y, uint8_t raw)
{
    game->map.bytes[map_floor_offset(floor) + y * MAP_TILE_COLS + x] = raw;
}

static uint8_t get_tile(const Game *game, int floor, int x, int y)
{
    return game->map.bytes[map_floor_offset(floor) + y * MAP_TILE_COLS + x];
}

static void place(Game *game, int floor, int x, int y)
{
    state_set(&game->state, ST_FLOOR, floor);
    state_set(&game->state, ST_X, x);
    state_set(&game->state, ST_Y, y);
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
    MapBlock map;
    make_map(&map);

    Game game;
    char msg[256];

    game_new(&game, &map);
    CHECK(game.has_state);
    CHECK(game_floor(&game) == 1);
    CHECK(game_x(&game) == 5 && game_y(&game) == 10);
    CHECK(game_stat(&game, ST_LIFE) == 1000);
    CHECK(game_stat(&game, ST_ATTACK) == 100);
    CHECK(game_stat(&game, ST_DEFENSE) == 100);
    CHECK(game_stat(&game, ST_FACING) == 4);

    /* Wall blocks movement. */
    place(&game, 1, 5, 5);
    set_tile(&game, 1, 6, 5, 0x08);
    msg[0] = '\0';
    CHECK(game_move(&game, 1, 0, msg, sizeof(msg)) == GAME_MOVE_BLOCKED);
    CHECK(game_x(&game) == 5 && game_y(&game) == 5);
    CHECK(game_stat(&game, ST_FACING) == 3);
    CHECK(strstr(msg, "blocked") != NULL);

    /* Ordinary floor moves and facing updates. */
    set_tile(&game, 1, 6, 5, 0x06);
    CHECK(game_move(&game, 1, 0, msg, sizeof(msg)) == GAME_MOVE_OK);
    CHECK(game_x(&game) == 6 && game_y(&game) == 5);
    CHECK(game_stat(&game, ST_FACING) == 3);

    place(&game, 1, 5, 5);
    CHECK(game_move(&game, -1, 0, msg, sizeof(msg)) == GAME_MOVE_OK);
    CHECK(game_stat(&game, ST_FACING) == 2);
    CHECK(game_move(&game, 0, 1, msg, sizeof(msg)) == GAME_MOVE_OK);
    CHECK(game_stat(&game, ST_FACING) == 1);
    CHECK(game_move(&game, 0, -1, msg, sizeof(msg)) == GAME_MOVE_OK);
    CHECK(game_stat(&game, ST_FACING) == 4);

    /* Key pickup. */
    place(&game, 1, 5, 5);
    set_tile(&game, 1, 6, 5, 0x1d);
    state_set(&game.state, ST_KEY_YELLOW, 0);
    CHECK(game_move(&game, 1, 0, msg, sizeof(msg)) == GAME_MOVE_KEY_PICKUP);
    CHECK(game_stat(&game, ST_KEY_YELLOW) == 1);
    CHECK(get_tile(&game, 1, 6, 5) == 0x06);
    CHECK(game_x(&game) == 6);

    /* Key pickup refuses to overflow a full counter. */
    place(&game, 1, 5, 5);
    set_tile(&game, 1, 6, 5, 0x1e);
    state_set(&game.state, ST_KEY_RED, INT32_MAX);
    CHECK(game_move(&game, 1, 0, msg, sizeof(msg)) == GAME_MOVE_BLOCKED);
    CHECK(game_stat(&game, ST_KEY_RED) == INT32_MAX);
    CHECK(get_tile(&game, 1, 6, 5) == 0x1e);
    CHECK(game_x(&game) == 5);

    /* Door opens and consumes the matching key. */
    place(&game, 1, 5, 5);
    set_tile(&game, 1, 6, 5, 0x01);
    state_set(&game.state, ST_KEY_YELLOW, 1);
    CHECK(game_move(&game, 1, 0, msg, sizeof(msg)) == GAME_MOVE_DOOR_OPENED);
    CHECK(game_stat(&game, ST_KEY_YELLOW) == 0);
    CHECK(get_tile(&game, 1, 6, 5) == 0x06);
    CHECK(game_x(&game) == 6);

    /* Door without the key stays closed. */
    place(&game, 1, 5, 5);
    set_tile(&game, 1, 6, 5, 0x02);
    state_set(&game.state, ST_KEY_BLUE, 0);
    CHECK(game_move(&game, 1, 0, msg, sizeof(msg)) == GAME_MOVE_BLOCKED);
    CHECK(get_tile(&game, 1, 6, 5) == 0x02);
    CHECK(game_x(&game) == 5);
    CHECK(strstr(msg, "blue") != NULL);

    /* Lava remains blocked until the Snow Crystal use handler exists. */
    place(&game, 1, 5, 5);
    set_tile(&game, 1, 6, 5, 0x0d);
    state_set(&game.state, ST_SPECIAL_FIRST + 11, 1);
    CHECK(game_move(&game, 1, 0, msg, sizeof(msg)) == GAME_MOVE_LAVA_BLOCKED);
    CHECK(game_x(&game) == 5);

    /* Monsters and unimplemented items/events block movement. */
    place(&game, 1, 5, 5);
    set_tile(&game, 1, 6, 5, 0x3d);
    CHECK(game_move(&game, 1, 0, msg, sizeof(msg)) == GAME_MOVE_MONSTER);
    set_tile(&game, 1, 6, 5, 0x20);
    CHECK(game_move(&game, 1, 0, msg, sizeof(msg)) == GAME_MOVE_ITEM);
    set_tile(&game, 1, 6, 5, 0x13);
    CHECK(game_move(&game, 1, 0, msg, sizeof(msg)) == GAME_MOVE_EVENT);
    CHECK(game_x(&game) == 5);

    /* Normal up/down stairs and floor limit update. */
    place(&game, 1, 5, 5);
    set_tile(&game, 1, 6, 5, 0x0b);
    game.map.bytes[map_stair_up_offset(2)] = 7 * MAP_TILE_COLS + 8;
    game.map.bytes[map_stair_down_offset(1)] = 3 * MAP_TILE_COLS + 4;
    CHECK(game_move(&game, 1, 0, msg, sizeof(msg)) == GAME_MOVE_STAIRS_UP);
    CHECK(game_floor(&game) == 2);
    CHECK(game_x(&game) == 8 && game_y(&game) == 7);
    CHECK(game_stat(&game, ST_FLOOR_LIMIT) == 2);

    set_tile(&game, 2, 9, 7, 0x0c);
    CHECK(game_move(&game, 1, 0, msg, sizeof(msg)) == GAME_MOVE_STAIRS_DOWN);
    CHECK(game_floor(&game) == 1);
    CHECK(game_x(&game) == 4 && game_y(&game) == 3);
    CHECK(game_stat(&game, ST_FLOOR_LIMIT) == 2);

    /* 43 -> 45 and 45 -> 43 skips. */
    place(&game, 43, 5, 5);
    set_tile(&game, 43, 6, 5, 0x0b);
    game.map.bytes[map_stair_up_offset(45)] = 2 * MAP_TILE_COLS + 3;
    CHECK(game_move(&game, 1, 0, msg, sizeof(msg)) == GAME_MOVE_STAIRS_UP);
    CHECK(game_floor(&game) == 45);
    CHECK(game_x(&game) == 3 && game_y(&game) == 2);

    set_tile(&game, 45, 4, 2, 0x0c);
    game.map.bytes[map_stair_down_offset(43)] = 6 * MAP_TILE_COLS + 1;
    CHECK(game_move(&game, 1, 0, msg, sizeof(msg)) == GAME_MOVE_STAIRS_DOWN);
    CHECK(game_floor(&game) == 43);
    CHECK(game_x(&game) == 1 && game_y(&game) == 6);

    /* Save snapshot and round-trip, including a distinguishable tile and
     * unknown metadata sentinels that must be preserved. */
    place(&game, 3, 2, 9);
    set_tile(&game, 3, 2, 9, 0x0e);
    state_set(&game.state, ST_LIFE, 777);
    state_set(&game.state, ST_KEY_RED, 2);
    state_set(&game.state, 166, 0x12345678);
    state_set(&game.state, 167, -7);
    SaveData snapshot;
    game_snapshot(&game, &snapshot);
    const char *path = "test_game_save.dat";
    char err[256] = "";
    CHECK(save_write_file(&snapshot, path, err, sizeof(err)));

    SaveData loaded;
    CHECK(save_load_file(path, &loaded, err, sizeof(err)));
    CHECK(loaded.status == STATE_OK);
    CHECK(state_get(&loaded.state, ST_FLOOR) == 3);
    CHECK(state_get(&loaded.state, ST_X) == 2);
    CHECK(state_get(&loaded.state, ST_Y) == 9);
    CHECK(state_get(&loaded.state, ST_LIFE) == 777);
    CHECK(state_get(&loaded.state, ST_KEY_RED) == 2);
    CHECK(state_get(&loaded.state, 166) == 0x12345678);
    CHECK(state_get(&loaded.state, 167) == -7);
    CHECK(map_tile(&loaded.map, 3, 9 * MAP_TILE_COLS + 2) == 0x0e);

    /* A pre-existing old-style .tmp path must not be reused or truncated. */
    FILE *side = fopen("test_game_save.dat.tmp", "w");
    CHECK(side != NULL);
    fputs("keep", side);
    fclose(side);
    CHECK(save_write_file(&snapshot, path, err, sizeof(err)));
    side = fopen("test_game_save.dat.tmp", "r");
    CHECK(side != NULL);
    char sentinel[8] = "";
    CHECK(fgets(sentinel, sizeof(sentinel), side) != NULL);
    fclose(side);
    CHECK(strcmp(sentinel, "keep") == 0);

    CHECK(!save_write_file(&snapshot, "save0.dat", err, sizeof(err)));
    remove("test_game_save.dat.tmp");
    remove(path);

    printf("test_game: ok\n");
    return 0;
}
