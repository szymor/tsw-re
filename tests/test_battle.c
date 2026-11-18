#include "battle.h"
#include "game.h"
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
    state_set(&game->state, ST_LIFE, 10000);
    state_set(&game->state, ST_ATTACK, 100);
    state_set(&game->state, ST_DEFENSE, 100);
    state_set(&game->state, ST_OPTION_A, 0);
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
    setup(&game, &map);

    /* The table itself is checked against the original's in
     * test_original; the fights in the monster_all_* vectors. */
    CHECK(strcmp(MONSTER_TABLE[0].name, "Green Slime") == 0);
    CHECK(MONSTER_TABLE[19].hp == 1500);

    /* Orb of the Hero row text: stats times (difficulty + 1). */
    char row[160];
    state_set(&game.state, ST_OPTION_A, 1);
    battle_hero_row_text(&game, 0, 12, row, sizeof(row));
    CHECK(strstr(row, "HP 70 ATK 36 DEF 2") != NULL && strstr(row, "damage 12") != NULL);
    battle_hero_row_text(&game, 0, -1, row, sizeof(row));
    CHECK(strstr(row, "cannot") != NULL);

    printf("test_battle: ok\n");
    return 0;
}
