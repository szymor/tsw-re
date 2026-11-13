#ifndef TSW_GAME_H
#define TSW_GAME_H

#include "map.h"
#include "save.h"
#include "state.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    MapBlock map;
    StateBlock state;
    bool has_state;
} Game;

void game_init(Game *game);
void game_new(Game *game, const MapBlock *initial_map);
void game_load(Game *game, const SaveData *save);
void game_snapshot(const Game *game, SaveData *out);

int game_floor(const Game *game);
int game_x(const Game *game);
int game_y(const Game *game);
int32_t game_stat(const Game *game, int index);

/* Movement, items, doors, battles and stairs are the original's handan
 * (0x444490), run as builders on the command VM (src/events/builders.lua). */

#endif
