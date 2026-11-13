#include "game.h"
#include "map.h"
#include "save.h"
#include "state.h"

#include <stdint.h>
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

    game_new(&game, &map);
    CHECK(game.has_state);
    CHECK(game_floor(&game) == 1);
    CHECK(game_x(&game) == 5 && game_y(&game) == 10);
    CHECK(game_stat(&game, ST_LIFE) == 1000);
    /* syokidata: the opening starts with the Sacred Sword and Shield. */
    CHECK(game_stat(&game, ST_WEAPON_LEVEL) == 5);
    CHECK(game_stat(&game, ST_SHIELD_LEVEL) == 5);
    CHECK(game_stat(&game, ST_SWORD_FLAG_FIRST + 4) == 1);
    CHECK(game_stat(&game, ST_SHIELD_FLAG_FIRST + 4) == 1);
    CHECK(game_stat(&game, ST_ATTACK) == 100);
    CHECK(game_stat(&game, ST_DEFENSE) == 100);
    CHECK(game_stat(&game, ST_FACING) == 4);

    /* Movement, doors, items, battles and stairs are handan builders,
     * checked against the original by test_event_builders. */

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
