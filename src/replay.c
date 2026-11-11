#include "replay.h"

#include "battle.h"
#include "game.h"
#include "items.h"
#include "map.h"
#include "save.h"
#include "state.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REPLAY_LINE 512

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s))
        s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return s;
}

static bool parse_long(const char *text, long minimum, long maximum, long *out)
{
    if (!text || !*text)
        return false;
    errno = 0;
    char *end = NULL;
    long value = strtol(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0')
        return false;
    if (value < minimum || value > maximum)
        return false;
    *out = value;
    return true;
}

static bool next_long(char **cursor, long minimum, long maximum, long *out)
{
    char *p = *cursor;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (!*p)
        return false;
    errno = 0;
    char *end = NULL;
    long value = strtol(p, &end, 0);
    if (errno != 0 || end == p)
        return false;
    if (*end && !isspace((unsigned char)*end))
        return false;
    if (value < minimum || value > maximum)
        return false;
    *cursor = end;
    *out = value;
    return true;
}

static bool load_initial_map(const char *data_dir, MapBlock *map,
                             char *err, size_t errsz)
{
    char path[1200];
    int written = snprintf(path, sizeof(path), "%s/maps/initial.bin", data_dir);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        if (err && errsz) snprintf(err, errsz, "data path is too long");
        return false;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (err && errsz) snprintf(err, errsz, "cannot open %s", path);
        return false;
    }
    uint8_t data[MAP_BLOCK_SIZE + 1];
    size_t n = fread(data, 1, sizeof(data), f);
    fclose(f);
    return map_parse(data, n, map, err, errsz);
}

static int32_t expected_stat(const Game *game, const char *name, bool *ok)
{
    *ok = true;
    if (strncmp(name, "slot", 4) == 0) {
        if (name[4] == '\0') {
            *ok = false;
            return 0;
        }
        errno = 0;
        char *end = NULL;
        long slot = strtol(name + 4, &end, 10);
        if (errno == 0 && end != name + 4 && *end == '\0' &&
            slot >= 0 && slot < SPECIAL_SLOT_COUNT)
            return game_stat(game, ST_SPECIAL_FIRST + (int)slot);
        *ok = false;
        return 0;
    }
    if (strcmp(name, "floor") == 0) return game_floor(game);
    if (strcmp(name, "x") == 0) return game_x(game);
    if (strcmp(name, "y") == 0) return game_y(game);
    if (strcmp(name, "life") == 0) return game_stat(game, ST_LIFE);
    if (strcmp(name, "attack") == 0) return game_stat(game, ST_ATTACK);
    if (strcmp(name, "defense") == 0) return game_stat(game, ST_DEFENSE);
    if (strcmp(name, "gold") == 0) return game_stat(game, ST_GOLD);
    if (strcmp(name, "limit") == 0) return game_stat(game, ST_FLOOR_LIMIT);
    if (strcmp(name, "yellow") == 0) return game_stat(game, ST_KEY_YELLOW);
    if (strcmp(name, "red") == 0) return game_stat(game, ST_KEY_RED);
    if (strcmp(name, "blue") == 0) return game_stat(game, ST_KEY_BLUE);
    *ok = false;
    return 0;
}

static void replay_print(void *user, const char *line)
{
    (void)user;
    printf("%s\n", line);
}

static void print_game(const Game *game)
{
    printf("floor=%d x=%d y=%d life=%d attack=%d defense=%d gold=%d "
           "keys=%d/%d/%d limit=%d\n",
           game_floor(game), game_x(game), game_y(game),
           game_stat(game, ST_LIFE), game_stat(game, ST_ATTACK),
           game_stat(game, ST_DEFENSE), game_stat(game, ST_GOLD),
           game_stat(game, ST_KEY_YELLOW), game_stat(game, ST_KEY_RED),
           game_stat(game, ST_KEY_BLUE), game_stat(game, ST_FLOOR_LIMIT));
}

static bool reject_pending_floor_event(const Game *game, int line_no,
                                      char *err, size_t errsz)
{
    if (game_floor(game) == 10 &&
        state_get(&game->state, ST_EVENT_FLAG_FLOOR10) == 0) {
        if (err && errsz)
            snprintf(err, errsz,
                     "line %d: floor-10 event is not supported in replay mode",
                     line_no);
        return true;
    }
    return false;
}

bool replay_run(const char *data_dir, const char *script_path,
                char *err, size_t errsz)
{
    if (!data_dir || !script_path) {
        if (err && errsz) snprintf(err, errsz, "missing replay path");
        return false;
    }

    FILE *script = fopen(script_path, "r");
    if (!script) {
        if (err && errsz) snprintf(err, errsz, "cannot open %s", script_path);
        return false;
    }

    Game game;
    game_init(&game);
    bool initialized = false;
    char line[REPLAY_LINE];
    int line_no = 0;
    bool ok = true;

    while (ok && fgets(line, sizeof(line), script)) {
        line_no++;
        char *text = trim(line);
        if (!*text || *text == '#')
            continue;

        char *arg = text;
        while (*arg && !isspace((unsigned char)*arg))
            arg++;
        if (*arg)
            *arg++ = '\0';
        arg = trim(arg);

        if (strcmp(text, "load") == 0) {
            if (!*arg) {
                snprintf(err, errsz, "line %d: load needs a path", line_no);
                ok = false;
                break;
            }
            SaveData save;
            if (!save_load_file(arg, &save, err, errsz)) {
                char tmp[256];
                snprintf(tmp, sizeof(tmp), "line %d: ", line_no);
                char detail[256];
                snprintf(detail, sizeof(detail), "%s", err ? err : "");
                snprintf(err, errsz, "%s%s", tmp, detail);
                ok = false;
                break;
            }
            game_load(&game, &save);
            initialized = true;
            if (reject_pending_floor_event(&game, line_no, err, errsz)) {
                ok = false;
                break;
            }
            printf("loaded %s (%s)\n", arg,
                   state_status_string(save.status));
        } else if (strcmp(text, "new") == 0) {
            MapBlock map;
            if (!load_initial_map(data_dir, &map, err, errsz)) {
                ok = false;
                break;
            }
            game_new(&game, &map);
            initialized = true;
            printf("new game\n");
        } else if (strcmp(text, "move") == 0) {
            if (!initialized) {
                snprintf(err, errsz, "line %d: game not initialized", line_no);
                ok = false;
                break;
            }
            int dx = 0, dy = 0;
            if (strcmp(arg, "up") == 0) dy = -1;
            else if (strcmp(arg, "down") == 0) dy = 1;
            else if (strcmp(arg, "left") == 0) dx = -1;
            else if (strcmp(arg, "right") == 0) dx = 1;
            else {
                snprintf(err, errsz, "line %d: unknown direction '%s'",
                         line_no, arg);
                ok = false;
                break;
            }
            char msg[256] = "";
            GameMoveResult result = game_move(&game, dx, dy, msg, sizeof(msg));
            if (result == GAME_MOVE_EVENT) {
                if ((game.event_raw == 0x07 && game.event_floor == 3 &&
                     game.event_index == 92) ||
                    (game.event_raw == 0x09 &&
                     ((game.event_floor == 5 && game.event_index == 118) ||
                      (game.event_floor == 9 && game.event_index == 53)))) {
                    game.map.bytes[map_floor_offset(game.event_floor) +
                                   game.event_index] = 0x06;
                    state_set(&game.state, ST_X,
                              game.event_index % MAP_TILE_COLS);
                    state_set(&game.state, ST_Y,
                              game.event_index / MAP_TILE_COLS);
                    state_compute_checksums(&game.state);
                    printf("event: %s wall opened\n",
                           game.event_raw == 0x09 ? "collapsed" : "floor-3");
                } else {
                    snprintf(err, errsz,
                             "line %d: events are not supported in replay mode "
                             "(floor %d index %d)",
                             line_no, game.event_floor, game.event_index);
                    ok = false;
                    break;
                }
            }
            if (msg[0])
                printf("move %s: %s\n", arg, msg);
            if (reject_pending_floor_event(&game, line_no, err, errsz)) {
                ok = false;
                break;
            }
        } else if (strcmp(text, "expect") == 0) {
            if (!initialized) {
                snprintf(err, errsz, "line %d: game not initialized", line_no);
                ok = false;
                break;
            }
            if (strncmp(arg, "tile ", 5) == 0) {
                char *cursor = arg + 5;
                long floor = 0, index = 0, value = 0;
                if (!next_long(&cursor, 1, MAP_FLOOR_COUNT - 1, &floor) ||
                    !next_long(&cursor, 0, MAP_TILES - 1, &index) ||
                    !next_long(&cursor, 0, 255, &value) ||
                    *trim(cursor) != '\0') {
                    snprintf(err, errsz, "line %d: malformed tile expect",
                             line_no);
                    ok = false;
                    break;
                }
                uint8_t actual = map_tile(&game.map, (int)floor, (int)index);
                if (actual != (uint8_t)value) {
                    snprintf(err, errsz,
                             "line %d: tile[%ld][%ld] is %d, expected %ld",
                             line_no, floor, index, actual, value);
                    ok = false;
                    break;
                }
            } else {
                char name[32] = "";
                int name_len = 0;
                if (sscanf(arg, "%31s%n", name, &name_len) != 1) {
                    snprintf(err, errsz, "line %d: malformed expect", line_no);
                    ok = false;
                    break;
                }
                long value = 0;
                if (!parse_long(trim(arg + name_len),
                                INT32_MIN, INT32_MAX, &value)) {
                    snprintf(err, errsz, "line %d: invalid expect value",
                             line_no);
                    ok = false;
                    break;
                }
                bool known = false;
                int32_t actual = expected_stat(&game, name, &known);
                if (!known) {
                    snprintf(err, errsz, "line %d: unknown field '%s'",
                             line_no, name);
                    ok = false;
                    break;
                }
                if (actual != value) {
                    snprintf(err, errsz,
                             "line %d: %s is %d, expected %ld",
                             line_no, name, actual, value);
                    ok = false;
                    break;
                }
            }
        } else if (strcmp(text, "use") == 0) {
            if (!initialized) {
                snprintf(err, errsz, "line %d: game not initialized", line_no);
                ok = false;
                break;
            }
            long slot = 0;
            if (!parse_long(arg, 0, SPECIAL_SLOT_COUNT - 1, &slot)) {
                snprintf(err, errsz, "line %d: invalid item slot", line_no);
                ok = false;
                break;
            }
            char msg[256] = "";
            ItemUseResult result = item_use_special(&game, (int)slot,
                                                    msg, sizeof(msg));
            if (result == ITEM_USE_FORECAST)
                battle_forecast_floor(&game, replay_print, NULL);
            if (msg[0])
                printf("use %ld: %s\n", slot, msg);
            if (reject_pending_floor_event(&game, line_no, err, errsz)) {
                ok = false;
                break;
            }
        } else if (strcmp(text, "save") == 0) {
            if (!initialized) {
                snprintf(err, errsz, "line %d: game not initialized", line_no);
                ok = false;
                break;
            }
            if (!*arg) {
                snprintf(err, errsz, "line %d: save needs a path", line_no);
                ok = false;
                break;
            }
            if (state_get(&game.state, ST_LIFE) <= 0) {
                snprintf(err, errsz, "line %d: cannot save while dead", line_no);
                ok = false;
                break;
            }
            SaveData save;
            game_snapshot(&game, &save);
            if (!save_write_file(&save, arg, err, errsz)) {
                ok = false;
                break;
            }
            printf("saved %s\n", arg);
        } else if (strcmp(text, "print") == 0) {
            if (!initialized) {
                snprintf(err, errsz, "line %d: game not initialized", line_no);
                ok = false;
                break;
            }
            print_game(&game);
        } else {
            snprintf(err, errsz, "line %d: unknown command '%s'",
                     line_no, text);
            ok = false;
        }
    }

    bool read_error = ferror(script) != 0;
    fclose(script);
    if (read_error) {
        if (err && errsz)
            snprintf(err, errsz, "read error in %s", script_path);
        return false;
    }
    return ok;
}
