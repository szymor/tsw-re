#ifndef TSW_ITEMS_H
#define TSW_ITEMS_H

#include "game.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SPECIAL_SLOT_COUNT 15

typedef enum {
    ITEM_USE_EMPTY = 0,
    ITEM_USE_PASSIVE,
    ITEM_USE_OK,
    ITEM_USE_FAILED,
    ITEM_USE_FORECAST
} ItemUseResult;

const char *special_item_name(int slot);
int special_item_icon(int slot);
const char *sword_name(int level);
const char *shield_name(int level);

/* Apply a map pickup effect for raw 0x20..0x3c.  The caller clears the map
 * tile and moves the player after this returns true. */
bool item_apply_pickup(Game *game, uint8_t raw, char *msg, size_t msgsz);

ItemUseResult item_use_special(Game *game, int slot,
                               char *msg, size_t msgsz);

#endif
