#include "battle.h"

#include "map.h"
#include "state.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>


int monster_index_from_raw(uint8_t raw)
{
    if (raw < 0x3d || raw > 0x5a)
        return -1;
    return ((raw + 1) >> 1) - 31;
}

BattleResult battle_simulate(const Game *game, int monster_index)
{
    BattleResult result = { false, 0, 0, 0 };
    if (!game || monster_index < 0 || monster_index >= MONSTER_COUNT)
        return result;

    const MonsterDef *m = &MONSTER_TABLE[monster_index];
    int64_t diff = (int64_t)state_get(&game->state, ST_OPTION_A) + 1;
    if (diff < 1) diff = 1;

    int multiplier = 1;
    if ((monster_index == 12 || monster_index == 13 ||
         monster_index == 17) &&
        state_get(&game->state, ST_SPECIAL_FIRST + 3) != 0)
        multiplier = 2;
    if (monster_index == 19 &&
        state_get(&game->state, ST_SPECIAL_FIRST + 10) != 0)
        multiplier = 2;

    int64_t monster_hp = (int64_t)m->hp * diff;
    int64_t monster_def = (int64_t)m->defense * diff;
    int64_t player_attack = (int64_t)state_get(&game->state, ST_ATTACK) * multiplier;
    if (monster_def >= player_attack)
        return result;

    int64_t damage_per_turn = player_attack - monster_def;
    int64_t turns64 = (monster_hp + damage_per_turn - 1) / damage_per_turn;
    int64_t player_defense = state_get(&game->state, ST_DEFENSE);
    int64_t per_hit = (int64_t)m->attack * diff - player_defense;
    if (per_hit < 0) per_hit = 0;

    int64_t damage = 0;
    if (turns64 > 1) {
        int64_t hits = turns64 - 1;
        if (per_hit > INT64_MAX / hits)
            damage = INT64_MAX;
        else
            damage = per_hit * hits;
    }
    if (damage > INT32_MAX) damage = INT32_MAX;

    int64_t lucky = state_get(&game->state, ST_SPECIAL_FIRST + 14);
    int64_t reward = (int64_t)m->gold * (lucky > 0 ? lucky + 1 : 1);
    if (reward > INT32_MAX) reward = INT32_MAX;

    result.can_win = true;
    result.turns = turns64 > INT_MAX ? INT_MAX : (int)turns64;
    result.damage_to_player = (int)damage;
    result.reward_gold = (int)reward;
    return result;
}

void battle_forecast_floor(const Game *game,
                           void (*log_line)(void *user, const char *line),
                           void *user)
{
    if (!game || !log_line)
        return;
    bool seen[MONSTER_COUNT] = { false };
    int floor = game_floor(game);
    char buf[160];

    for (int i = 0; i < MAP_TILES; i++) {
        int index = monster_index_from_raw(map_tile(&game->map, floor, i));
        if (index < 0 || index >= MONSTER_COUNT || seen[index])
            continue;
        seen[index] = true;
        const MonsterDef *m = &MONSTER_TABLE[index];
        BattleResult r = battle_simulate(game, index);
        int64_t diff = (int64_t)state_get(&game->state, ST_OPTION_A) + 1;
        if (diff < 1) diff = 1;
        long long hp = (long long)((int64_t)m->hp * diff);
        long long atk = (long long)((int64_t)m->attack * diff);
        long long def = (long long)((int64_t)m->defense * diff);
        if (r.can_win)
            snprintf(buf, sizeof(buf),
                     "%s: HP %lld ATK %lld DEF %lld  damage %d, gold %d",
                     m->name, hp, atk, def,
                     r.damage_to_player, r.reward_gold);
        else
            snprintf(buf, sizeof(buf),
                     "%s: HP %lld ATK %lld DEF %lld  cannot be defeated yet",
                     m->name, hp, atk, def);
        log_line(user, buf);
    }
}
