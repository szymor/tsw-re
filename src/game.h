#ifndef TSW_GAME_H
#define TSW_GAME_H

#include "map.h"
#include "save.h"
#include "state.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    GAME_MOVE_OK = 0,
    GAME_MOVE_BLOCKED,
    GAME_MOVE_DOOR_OPENED,
    GAME_MOVE_KEY_PICKUP,
    GAME_MOVE_STAIRS_UP,
    GAME_MOVE_STAIRS_DOWN,
    GAME_MOVE_LAVA_BLOCKED,
    GAME_MOVE_MONSTER,
    GAME_MOVE_EVENT,
    GAME_MOVE_ITEM
} GameMoveResult;

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

/* Attempt to move one tile.  On success the state/map may be mutated.  A
 * human-readable result is written to msg (may be empty for a plain move). */
GameMoveResult game_move(Game *game, int dx, int dy,
                         char *msg, size_t msgsz);

#endif
