#include "battle.h"

/* Monster records shared by the battle simulator and the event builders
 * (taisen2 needs the same HP/attack/defense/gold values as the original). */
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
