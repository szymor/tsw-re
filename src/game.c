#include "game.h"

#include <stdio.h>
#include <string.h>

void game_init(Game *game)
{
    if (!game) return;
    memset(game, 0, sizeof(*game));
}

void game_new(Game *game, const MapBlock *initial_map)
{
    if (!game) return;
    memset(game, 0, sizeof(*game));
    if (initial_map)
        game->map = *initial_map;

    /* syokidata (0x455cb8) on a zeroed block: 1000/100/100, floor 1 at
     * (5,10), facing 4, and the Sacred Sword and Shield equipped (display
     * level 5, "picked" flags set); openning31 takes them away after Zeno
     * (17-static-findings.md §1). */
    state_syokidata(&game->state);
    state_set(&game->state, ST_MAGIC, STATE_MAGIC_VALUE);
    state_set(&game->state, ST_VERSION, STATE_VERSION_VALUE);
    state_compute_checksums(&game->state);
    game->has_state = true;
}

void game_load(Game *game, const SaveData *save)
{
    if (!game || !save) return;
    game->map = save->map;
    game->state = save->state;
    game->has_state = true;

    int floor = (int)state_get(&game->state, ST_FLOOR);
    if (floor < 1 || floor > 50)
        state_set(&game->state, ST_FLOOR, 1);
    int x = (int)state_get(&game->state, ST_X);
    int y = (int)state_get(&game->state, ST_Y);
    if (x < 0 || x >= MAP_TILE_COLS)
        state_set(&game->state, ST_X, 5);
    if (y < 0 || y >= MAP_TILE_ROWS)
        state_set(&game->state, ST_Y, 10);
    state_compute_checksums(&game->state);
}

void game_snapshot(const Game *game, SaveData *out)
{
    if (!game || !out) return;
    memset(out, 0, sizeof(*out));
    out->map = game->map;
    out->state = game->state;
    state_compute_checksums(&out->state);
    out->status = STATE_OK;
}

int game_floor(const Game *game)
{
    return game ? (int)state_get(&game->state, ST_FLOOR) : 0;
}

int game_x(const Game *game)
{
    return game ? (int)state_get(&game->state, ST_X) : 0;
}

int game_y(const Game *game)
{
    return game ? (int)state_get(&game->state, ST_Y) : 0;
}

int32_t game_stat(const Game *game, int index)
{
    return game ? state_get(&game->state, index) : 0;
}
