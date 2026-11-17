#include "items.h"

#include "itext.h"

#include <stddef.h>

const char *special_item_name(int slot)
{
    if (slot < 0 || slot >= SPECIAL_SLOT_COUNT)
        return "";
    return tx(ITX_ITEM_0 + slot);       /* interface.tsv item.N */
}

int special_item_icon(int slot)
{
    /* itemdisp() draws special slot s from atlas sprite 0x23 + s. */
    return 0x23 + slot;
}

const char *sword_name(int level)
{
    return level >= 1 && level <= 5 ? tx(ITX_SWORD_1 + level - 1) : "-";
}

const char *shield_name(int level)
{
    return level >= 1 && level <= 5 ? tx(ITX_SHIELD_1 + level - 1) : "-";
}
