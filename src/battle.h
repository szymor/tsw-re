#ifndef TSW_BATTLE_H
#define TSW_BATTLE_H

#include "game.h"

#include <stddef.h>

#define MONSTER_COUNT 33

typedef struct {
    const char *name;
    int hp;
    int attack;
    int defense;
    int gold;
} MonsterDef;

extern const MonsterDef MONSTER_TABLE[MONSTER_COUNT];

/* One Orb-of-the-Hero row (img6work/yousya): `damage` from obudameiji,
 * negative when the monster cannot be hurt. */
void battle_hero_row_text(const Game *game, int monster_index, int damage,
                          char *out, size_t outsz);

#endif
