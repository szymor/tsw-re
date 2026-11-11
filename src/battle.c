#include "battle.h"

#include "map.h"
#include "state.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>

const MonsterDef MONSTER_TABLE[MONSTER_COUNT] = {
    { "Green Slime", 35, 18, 1, 1 },
    { "Red Slime", 45, 20, 2, 2 },
    { "Big Slime", 130, 60, 3, 8 },
    { "Bat", 35, 38, 3, 3 },
    { "Priest", 60, 32, 8, 5 },
    { "Superion Priest", 100, 95, 30, 22 },
    { "Gate-Keeper C", 50, 48, 22, 12 },
    { "Gate-Keeper B", 100, 180, 110, 100 },
    { "Skeleton C", 50, 42, 6, 6 },
    { "Skeleton B", 55, 52, 12, 8 },
    { "Skeleton A", 100, 65, 15, 30 },
    { "Big Bat", 60, 100, 8, 12 },
    { "Zombie", 260, 85, 5, 18 },
    { "Zombie Knight", 320, 120, 15, 30 },
    { "Rock", 20, 100, 68, 28 },
    { "The Magic Sergeant, Zeno", 8000, 5000, 1000, 500 },
    { "The Magic Sergeant", 230, 450, 100, 100 },
    { "Vampire", 444, 199, 66, 144 },
    { "Giant Octopus", 1200, 180, 20, 100 },
    { "Dragon", 1500, 600, 250, 800 },
    { "Golden Knight", 120, 150, 50, 100 },
    { "Knight", 160, 230, 105, 65 },
    { "Swordsman", 100, 680, 50, 55 },
    { "Soldier", 210, 200, 65, 45 },
    { "Ghost Soldier", 220, 180, 30, 35 },
    { "Slime Man", 320, 140, 20, 30 },
    { "Great Magic Master", 4500, 560, 310, 1000 },
    { "Vampire Bat", 200, 390, 90, 50 },
    { "SlimeLord", 360, 310, 20, 40 },
    { "Magician A", 200, 380, 130, 90 },
    { "Magician B", 220, 370, 110, 80 },
    { "Dark Knight", 180, 430, 210, 120 },
    { "Gate-Keeper A", 180, 460, 360, 200 }
};

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
