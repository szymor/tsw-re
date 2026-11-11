#include "game.h"
#include "items.h"
#include "map.h"
#include "state.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void setup(Game *game, MapBlock *map)
{
    memset(map, 0, sizeof(*map));
    map->bytes[0] = 0x00;
    map->bytes[1] = 0x01;
    for (int floor = 1; floor < MAP_FLOOR_COUNT; floor++)
        memset(map->bytes + map_floor_offset(floor), 0x06, MAP_TILES);
    game_new(game, map);
    state_set(&game->state, ST_LIFE, 1000);
    state_set(&game->state, ST_ATTACK, 100);
    state_set(&game->state, ST_DEFENSE, 100);
    state_set(&game->state, ST_OPTION_A, 0);
}

static void set_tile(Game *game, int floor, int index, uint8_t raw)
{
    game->map.bytes[map_floor_offset(floor) + index] = raw;
}

static uint8_t get_tile(const Game *game, int floor, int index)
{
    return game->map.bytes[map_floor_offset(floor) + index];
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
    Game game;
    char msg[256];

    setup(&game, &map);
    CHECK(item_apply_pickup(&game, 0x20, msg, sizeof(msg)));
    CHECK(state_get(&game.state, ST_LIFE) == 1050);
    CHECK(item_apply_pickup(&game, 0x21, msg, sizeof(msg)));
    CHECK(state_get(&game.state, ST_LIFE) == 1250);
    CHECK(item_apply_pickup(&game, 0x22, msg, sizeof(msg)));
    CHECK(state_get(&game.state, ST_ATTACK) == 101);
    CHECK(item_apply_pickup(&game, 0x23, msg, sizeof(msg)));
    CHECK(state_get(&game.state, ST_DEFENSE) == 101);

    state_set(&game.state, ST_FLOOR, 11);
    int life = state_get(&game.state, ST_LIFE);
    CHECK(item_apply_pickup(&game, 0x20, msg, sizeof(msg)));
    CHECK(state_get(&game.state, ST_LIFE) == life + 100);

    setup(&game, &map);
    state_set(&game.state, ST_OPTION_A, 1);
    CHECK(item_apply_pickup(&game, 0x20, msg, sizeof(msg)));
    CHECK(state_get(&game.state, ST_LIFE) == 1100);

    /* Equipment bonus and duplicate protection. */
    setup(&game, &map);
    CHECK(item_apply_pickup(&game, 0x33, msg, sizeof(msg)));
    CHECK(state_get(&game.state, ST_ATTACK) == 110);
    CHECK(state_get(&game.state, ST_WEAPON_LEVEL) == 1);
    CHECK(state_get(&game.state, ST_SWORD_FLAG_FIRST) == 1);
    CHECK(item_apply_pickup(&game, 0x33, msg, sizeof(msg)));
    CHECK(state_get(&game.state, ST_ATTACK) == 110);
    CHECK(item_apply_pickup(&game, 0x38, msg, sizeof(msg)));
    CHECK(state_get(&game.state, ST_DEFENSE) == 110);

    /* Original new-game Sacred flags start set without the stat bonus; the
     * still-present map pickup identifies the genuine first collection. */
    setup(&game, &map);
    state_set(&game.state, ST_WEAPON_LEVEL, 5);
    state_set(&game.state, ST_SWORD_FLAG_FIRST + 4, 1);
    state_set(&game.state, ST_ATTACK, 100);
    set_tile(&game, 13, 5 * MAP_TILE_COLS + 4, 0x37);
    CHECK(item_apply_pickup(&game, 0x37, msg, sizeof(msg)));
    CHECK(state_get(&game.state, ST_ATTACK) == 200);
    set_tile(&game, 13, 5 * MAP_TILE_COLS + 4, 0x06);
    CHECK(item_apply_pickup(&game, 0x37, msg, sizeof(msg)));
    CHECK(state_get(&game.state, ST_ATTACK) == 200);

    /* Picking a lower sword first must not lose the later Sacred bonus. */
    setup(&game, &map);
    set_tile(&game, 5, 0, 0x33);
    CHECK(item_apply_pickup(&game, 0x33, msg, sizeof(msg)));
    set_tile(&game, 5, 0, 0x06);
    state_set(&game.state, ST_SWORD_FLAG_FIRST + 4, 1); /* startup quirk */
    set_tile(&game, 13, 5 * MAP_TILE_COLS + 4, 0x37);
    CHECK(item_apply_pickup(&game, 0x37, msg, sizeof(msg)));
    CHECK(state_get(&game.state, ST_ATTACK) == 210);
    CHECK(state_get(&game.state, ST_WEAPON_LEVEL) == 5);

    setup(&game, &map);
    state_set(&game.state, ST_SHIELD_LEVEL, 5);
    state_set(&game.state, ST_SHIELD_FLAG_FIRST + 4, 1);
    state_set(&game.state, ST_DEFENSE, 100);
    set_tile(&game, 44, 5 * MAP_TILE_COLS + 5, 0x3c);
    CHECK(item_apply_pickup(&game, 0x3c, msg, sizeof(msg)));
    CHECK(state_get(&game.state, ST_DEFENSE) == 200);
    set_tile(&game, 44, 5 * MAP_TILE_COLS + 5, 0x06);
    CHECK(item_apply_pickup(&game, 0x3c, msg, sizeof(msg)));
    CHECK(state_get(&game.state, ST_DEFENSE) == 200);

    /* Special pickup values. */
    CHECK(item_apply_pickup(&game, 0x2b, msg, sizeof(msg)));
    CHECK(state_get(&game.state, ST_SPECIAL_FIRST + 7) == 3);
    CHECK(item_apply_pickup(&game, 0x25, msg, sizeof(msg)));
    CHECK(state_get(&game.state, ST_SPECIAL_FIRST + 1) == 1);

    /* Magic Key opens every door on the floor. */
    setup(&game, &map);
    set_tile(&game, 1, 10, 0x01);
    set_tile(&game, 1, 11, 0x03);
    state_set(&game.state, ST_SPECIAL_FIRST + 12, 1);
    CHECK(item_use_special(&game, 12, msg, sizeof(msg)) == ITEM_USE_OK);
    CHECK(get_tile(&game, 1, 10) == 0x06);
    CHECK(get_tile(&game, 1, 11) == 0x06);
    CHECK(state_get(&game.state, ST_SPECIAL_FIRST + 12) == 0);

    /* Super Mattock removes every wall on the floor. */
    set_tile(&game, 1, 20, 0x08);
    set_tile(&game, 1, 21, 0x0a);
    state_set(&game.state, ST_SPECIAL_FIRST + 13, 1);
    CHECK(item_use_special(&game, 13, msg, sizeof(msg)) == ITEM_USE_OK);
    CHECK(get_tile(&game, 1, 20) == 0x06);
    CHECK(get_tile(&game, 1, 21) == 0x06);

    /* Snow Crystal freezes lava. */
    set_tile(&game, 1, 30, 0x0d);
    state_set(&game.state, ST_SPECIAL_FIRST + 11, 1);
    CHECK(item_use_special(&game, 11, msg, sizeof(msg)) == ITEM_USE_OK);
    CHECK(get_tile(&game, 1, 30) == 0x0e);

    /* Magic Elixir consumes the item and increases life. */
    setup(&game, &map);
    state_set(&game.state, ST_SPECIAL_FIRST + 4, 1);
    int before = state_get(&game.state, ST_LIFE);
    CHECK(item_use_special(&game, 4, msg, sizeof(msg)) == ITEM_USE_OK);
    CHECK(state_get(&game.state, ST_LIFE) == before + 2000);
    CHECK(state_get(&game.state, ST_SPECIAL_FIRST + 4) == 0);

    /* Bomb kills adjacent monsters; mattock removes adjacent walls. */
    setup(&game, &map);
    int above = (game_y(&game) - 1) * MAP_TILE_COLS + game_x(&game);
    set_tile(&game, 1, above, 0x3d);
    state_set(&game.state, ST_SPECIAL_FIRST + 6, 1);
    CHECK(item_use_special(&game, 6, msg, sizeof(msg)) == ITEM_USE_OK);
    CHECK(get_tile(&game, 1, above) == 0x06);
    set_tile(&game, 1, above, 0x08);
    state_set(&game.state, ST_SPECIAL_FIRST + 5, 1);
    CHECK(item_use_special(&game, 5, msg, sizeof(msg)) == ITEM_USE_OK);
    CHECK(get_tile(&game, 1, above) == 0x06);

    /* Warp Staff moves to the up-stairs and consumes one charge. */
    setup(&game, &map);
    set_tile(&game, 1, 50, 0x0b);
    state_set(&game.state, ST_SPECIAL_FIRST + 7, 3);
    CHECK(item_use_special(&game, 7, msg, sizeof(msg)) == ITEM_USE_OK);
    CHECK(game_x(&game) == 50 % MAP_TILE_COLS);
    CHECK(game_y(&game) == 50 / MAP_TILE_COLS);
    CHECK(state_get(&game.state, ST_SPECIAL_FIRST + 7) == 2);

    /* Wings move one floor and are consumed. */
    setup(&game, &map);
    game.map.bytes[map_stair_up_offset(2)] = 42;
    game.map.bytes[map_stair_down_offset(1)] = 24;
    state_set(&game.state, ST_SPECIAL_FIRST + 8, 1);
    CHECK(item_use_special(&game, 8, msg, sizeof(msg)) == ITEM_USE_OK);
    CHECK(game_floor(&game) == 2);
    CHECK(game_x(&game) == 42 % MAP_TILE_COLS && game_y(&game) == 42 / MAP_TILE_COLS);
    CHECK(state_get(&game.state, ST_SPECIAL_FIRST + 8) == 0);
    state_set(&game.state, ST_SPECIAL_FIRST + 9, 1);
    CHECK(item_use_special(&game, 9, msg, sizeof(msg)) == ITEM_USE_OK);
    CHECK(game_floor(&game) == 1);
    CHECK(state_get(&game.state, ST_SPECIAL_FIRST + 9) == 0);

    /* Extreme stat values saturate instead of wrapping. */
    state_set(&game.state, ST_LIFE, INT32_MAX);
    CHECK(item_apply_pickup(&game, 0x20, msg, sizeof(msg)));
    CHECK(state_get(&game.state, ST_LIFE) == INT32_MAX);
    state_set(&game.state, ST_ATTACK, INT32_MAX);
    CHECK(item_apply_pickup(&game, 0x33, msg, sizeof(msg)));
    CHECK(state_get(&game.state, ST_ATTACK) == INT32_MAX);
    state_set(&game.state, ST_DEFENSE, INT32_MAX);
    state_set(&game.state, ST_SHIELD_FLAG_FIRST, 0);
    CHECK(item_apply_pickup(&game, 0x38, msg, sizeof(msg)));
    CHECK(state_get(&game.state, ST_DEFENSE) == INT32_MAX);

    /* Dead characters cannot pick up or use items. */
    state_set(&game.state, ST_LIFE, 0);
    state_set(&game.state, ST_SPECIAL_FIRST + 3, 1);
    CHECK(!item_apply_pickup(&game, 0x20, msg, sizeof(msg)));
    CHECK(item_use_special(&game, 3, msg, sizeof(msg)) == ITEM_USE_FAILED);

    /* Passive items remain in the inventory. */
    state_set(&game.state, ST_LIFE, 1000);
    state_set(&game.state, ST_SPECIAL_FIRST + 3, 1);
    CHECK(item_use_special(&game, 3, msg, sizeof(msg)) == ITEM_USE_PASSIVE);
    CHECK(state_get(&game.state, ST_SPECIAL_FIRST + 3) == 1);

    printf("test_items: ok\n");
    return 0;
}
