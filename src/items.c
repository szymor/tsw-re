#include "items.h"

#include <stddef.h>

static const char *const SPECIAL_NAMES[SPECIAL_SLOT_COUNT] = {
    "Orb of the Hero", "Orb of Wisdom", "Orb of Flying", "Cross",
    "Magic Elixir", "Magic Mattock", "Bomb", "Warp Staff",
    "Wings up", "Wings down", "Dragon Slayer", "Snow Crystal",
    "Magic Key", "Super Mattock", "Lucky Gold"
};

const char *special_item_name(int slot)
{
    if (slot < 0 || slot >= SPECIAL_SLOT_COUNT)
        return "";
    return SPECIAL_NAMES[slot];
}

int special_item_icon(int slot)
{
    /* itemdisp() draws special slot s from atlas sprite 0x23 + s. */
    return 0x23 + slot;
}

const char *sword_name(int level)
{
    switch (level) {
    case 1: return "Iron Sword";
    case 2: return "Silver Sword";
    case 3: return "Knight Sword";
    case 4: return "Holy Sword";
    case 5: return "Sacred Sword";
    default: return "-";
    }
}

const char *shield_name(int level)
{
    switch (level) {
    case 1: return "Iron Shield";
    case 2: return "Silver Shield";
    case 3: return "Knight Shield";
    case 4: return "Holy Shield";
    case 5: return "Sacred Shield";
    default: return "-";
    }
}
