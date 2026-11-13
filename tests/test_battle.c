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

    CHECK(monster_index_from_raw(0x3d) == 0);
    CHECK(monster_index_from_raw(0x5a) == 14);
    CHECK(monster_index_from_raw(0x3c) == -1);
    CHECK(strcmp(MONSTER_TABLE[0].name, "Green Slime") == 0);
    CHECK(MONSTER_TABLE[19].hp == 1500);

    BattleResult slime = battle_simulate(&game, 0);
    CHECK(slime.can_win);
    CHECK(slime.reward_gold == 1);
    CHECK(slime.damage_to_player == 0);

    /* Cross reduces the number of turns and therefore damage from undead. */
    state_set(&game.state, ST_DEFENSE, 0);
    BattleResult zombie = battle_simulate(&game, 12);
    CHECK(zombie.can_win);
    CHECK(zombie.turns == 3);
    CHECK(zombie.damage_to_player == 170);
    state_set(&game.state, ST_SPECIAL_FIRST + 3, 1);
    zombie = battle_simulate(&game, 12);
    CHECK(zombie.can_win);
    CHECK(zombie.turns == 2);
    CHECK(zombie.damage_to_player == 85);

    /* Dragon Slayer doubles attack against the Dragon. */
    state_set(&game.state, ST_ATTACK, 200);
    state_set(&game.state, ST_DEFENSE, 100);
    BattleResult dragon = battle_simulate(&game, 19);
    CHECK(!dragon.can_win);
    state_set(&game.state, ST_SPECIAL_FIRST + 10, 1);
    dragon = battle_simulate(&game, 19);
    CHECK(dragon.can_win);

    /* Lucky Gold doubles the reward. */
    state_set(&game.state, ST_SPECIAL_FIRST + 14, 1);
    BattleResult rich = battle_simulate(&game, 0);
    CHECK(rich.reward_gold == 2);

    /* Difficulty option scales monster stats. */
    BattleResult big_normal = battle_simulate(&game, 2);
    CHECK(big_normal.turns == 1);
    state_set(&game.state, ST_OPTION_A, 1);
    BattleResult big_hard = battle_simulate(&game, 2);
    CHECK(big_hard.turns == 2);

    /* Extreme values saturate instead of overflowing. */
    state_set(&game.state, ST_OPTION_A, 0);
    state_set(&game.state, ST_ATTACK, INT32_MAX);
    state_set(&game.state, ST_SPECIAL_FIRST + 3, 1);
    CHECK(battle_simulate(&game, 12).can_win);
    state_set(&game.state, ST_OPTION_A, INT32_MAX);
    CHECK(!battle_simulate(&game, 0).can_win);
    state_set(&game.state, ST_OPTION_A, 999999);
    state_set(&game.state, ST_ATTACK, 1000001);
    state_set(&game.state, ST_DEFENSE, 18000000);
    BattleResult long_win = battle_simulate(&game, 0);
    CHECK(long_win.can_win);
    CHECK(long_win.turns == 35000000);
    CHECK(long_win.damage_to_player == 0);

    /* Unwinnable monster. */
    state_set(&game.state, ST_OPTION_A, 0);
    state_set(&game.state, ST_ATTACK, 1);
    CHECK(!battle_simulate(&game, 14).can_win);

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
