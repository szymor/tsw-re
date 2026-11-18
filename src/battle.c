#include "battle.h"

#include "state.h"

#include <stdio.h>

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
