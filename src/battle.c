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

void battle_hero_row_text(const Game *game, int monster_index, int damage,
                          char *out, size_t outsz)
{
    if (!out || !outsz)
        return;
    out[0] = '\0';
    if (!game || monster_index < 0 || monster_index >= MONSTER_COUNT)
        return;
    /* yousya (0x44f26c): name, HP/ATK/DEF times (difficulty + 1), then
     * obudameiji's damage (ListBox2 0xb8) or "cannot win" (0xb9). */
    const MonsterDef *m = &MONSTER_TABLE[monster_index];
    long long d = (long long)state_get(&game->state, ST_OPTION_A) + 1;
    if (damage >= 0)
        snprintf(out, outsz, "%s: HP %lld ATK %lld DEF %lld  damage %d",
                 m->name, m->hp * d, m->attack * d, m->defense * d, damage);
    else
        snprintf(out, outsz,
                 "%s: HP %lld ATK %lld DEF %lld  cannot be defeated yet",
                 m->name, m->hp * d, m->attack * d, m->defense * d);
}
