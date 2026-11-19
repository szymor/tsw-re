#ifndef TSW_ITEMS_H
#define TSW_ITEMS_H

#define SPECIAL_SLOT_COUNT 15
/* Special item slots (state[ST_SPECIAL_FIRST + slot]), 10-events-and-items.md. */
#define ITEM_SLOT_ORB_HERO 0

const char *special_item_name(int slot);
int special_item_icon(int slot);
const char *sword_name(int level);
const char *shield_name(int level);

#endif
