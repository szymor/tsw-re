#include "replay.h"

#include "battle.h"
#include "event_engine.h"
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

/* The headless event runtime: handan and every other handler run as
 * builders on the command VM, drained at once. */
static EventEngine replay_engine;

static void replay_log(void *user, const char *line)
{
    (void)user;
    printf("  %s\n", line);
}

static bool find_builders(const char *data_dir, char *out, size_t outsz)
{
    const char *candidates[3] = { NULL, NULL, "src/events/builders.lua" };
    char a[1200], b[1200];
    snprintf(a, sizeof(a), "%s/events/builders.lua", data_dir);
    candidates[0] = a;
#ifdef TSW_SOURCE_DIR
    snprintf(b, sizeof(b), "%s/src/events/builders.lua", TSW_SOURCE_DIR);
    candidates[1] = b;
#else
    (void)b;
#endif
    for (int i = 0; i < 3; i++) {
        FILE *f = candidates[i] ? fopen(candidates[i], "r") : NULL;
        if (f) {
            fclose(f);
            snprintf(out, outsz, "%s", candidates[i]);
            return true;
        }
    }
    return false;
}

/* A question the script must answer before the next move. */
static const char *pending_question(const EventEngine *e)
{
    if (e->death_prompt) return "try-again prompt (tryagain yes|no)";
    if (e->altar_open) return "altar menu (altar 0..3)";
    if (e->dialog_open && e->dialog_choice) return "question (choose yes|no)";
    if (e->item_confirm) return "item panel (item yes|no)";
    if (e->fly_open) return "flight panel (fly up|down|land)";
    if (e->ending_wait) return "ending picture (ending)";
    return NULL;
}

/* Let Timer2 consume the timeline; plain windows are confirmed and the
 * ranking form closed, as a player pressing Enter would. */
static void drain(EventEngine *e)
{
    static uint64_t now = 1000;
    for (int i = 0; i < 200000 && event_engine_active(e); i++) {
        if (pending_question(e))
            return;
        /* The ranking form: name prompt cancelled, then closed; a
         * ShowMessage is acknowledged. */
        if (e->ranking_name_entry)
            event_engine_ranking_name(e, NULL, false);
        if (e->ranking_open)
            event_engine_close_ranking(e);
        if (e->message_open)
            event_engine_close_message(e);
        if (e->dialog_open && !e->dialog_choice)
            event_engine_confirm(e);
        e->checkpoint_pending = false;
        now += 20;
        event_engine_update(e, now);
    }
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
    char builders[1200];
    if (!find_builders(data_dir, builders, sizeof(builders))) {
        if (err && errsz) snprintf(err, errsz, "cannot find events/builders.lua");
        fclose(script);
        return false;
    }
    EventEngine *en = &replay_engine;
    en->log_fn = replay_log;
    en->log_user = NULL;
    if (!event_engine_init(en, builders, err, errsz)) {
        fclose(script);
        return false;
    }
    char text_path[1200];
    snprintf(text_path, sizeof(text_path), "%s/text/messages.tsv", data_dir);
    event_engine_load_messages(en, text_path, NULL, 0);
    static MapBlock initial;
    bool have_initial = load_initial_map(data_dir, &initial, NULL, 0);
    if (have_initial)
        event_engine_set_initial_map(en, &initial);
    event_engine_bind(en, &game.state, &game.map);
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
            event_engine_cancel(en);
            initialized = true;
            printf("loaded %s (%s)\n", arg,
                   state_status_string(save.status));
        } else if (strcmp(text, "new") == 0) {
            MapBlock map;
            if (!load_initial_map(data_dir, &map, err, errsz)) {
                ok = false;
                break;
            }
            game_new(&game, &map);
            event_engine_cancel(en);
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
            const char *q = pending_question(en);
            if (q) {
                snprintf(err, errsz, "line %d: answer the %s first", line_no, q);
                ok = false;
                break;
            }
            /* formkeydown: turn, then handan on the cell ahead. */
            int facing = dy > 0 ? 1 : dx < 0 ? 2 : dx > 0 ? 3 : 4;
            state_set(&game.state, ST_FACING, facing);
            int nx = game_x(&game) + dx, ny = game_y(&game) + dy;
            if (nx >= 0 && nx < MAP_TILE_COLS && ny >= 0 && ny < MAP_TILE_ROWS) {
                int floor = game_floor(&game);
                int index = ny * MAP_TILE_COLS + nx;
                event_engine_trigger(en, floor, index,
                                     map_tile(&game.map, floor, index),
                                     dy < 0 ? 1 : dx < 0 ? 2 : dx > 0 ? 3 : 4);
                drain(en);
            }
            state_compute_checksums(&game.state);
            if (en->status_text[0])
                printf("move %s: %s\n", arg, en->status_text);
            en->status_text[0] = '\0';
        } else if (strcmp(text, "choose") == 0 || strcmp(text, "tryagain") == 0) {
            bool yes = strcmp(arg, "yes") == 0;
            if (!yes && strcmp(arg, "no") != 0) {
                snprintf(err, errsz, "line %d: expected yes or no", line_no);
                ok = false;
                break;
            }
            bool done = text[0] == 'c' ? event_engine_choose(en, yes)
                                       : event_engine_death_answer(en, yes);
            if (!done) {
                snprintf(err, errsz, "line %d: no such question is open", line_no);
                ok = false;
                break;
            }
            drain(en);
            state_compute_checksums(&game.state);
        } else if (strcmp(text, "altar") == 0) {
            long option = 0;
            if (!parse_long(arg, 0, 3, &option) ||
                !event_engine_altar_choose(en, (int)option)) {
                snprintf(err, errsz, "line %d: no altar menu or bad option", line_no);
                ok = false;
                break;
            }
            drain(en);
            state_compute_checksums(&game.state);
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
            const char *q = pending_question(en);
            if (q) {
                snprintf(err, errsz, "line %d: answer the %s first", line_no, q);
                ok = false;
                break;
            }
            /* formkeydown Enter on the item overlay: img6work .. img17work. */
            EventEngineResult r = event_engine_use_item(en, (int)slot);
            if (r == EVENT_ENGINE_NOT_ROUTED) {
                printf("use %ld: %s works by itself\n", slot,
                       special_item_name((int)slot));
            } else if (r == EVENT_ENGINE_STARTED) {
                for (int i = 0; en->hero_orb_pending && i < en->hero_rows; i++) {
                    char row[160];
                    battle_hero_row_text(&game, en->hero_row_index[i],
                                         en->hero_row_damage[i], row,
                                         sizeof(row));
                    printf("%s\n", row);
                }
                en->hero_orb_pending = false;
                en->wisdom_orb_pending = false;
                drain(en);
            }
            state_compute_checksums(&game.state);
        } else if (strcmp(text, "ending") == 0) {
            if (!event_engine_ending_continue(en)) {
                snprintf(err, errsz, "line %d: the ending is not waiting", line_no);
                ok = false;
                break;
            }
            drain(en);
            state_compute_checksums(&game.state);
        } else if (strcmp(text, "item") == 0 || strcmp(text, "fly") == 0) {
            bool done;
            if (text[0] == 'i') {
                bool yes = strcmp(arg, "yes") == 0;
                if (!yes && strcmp(arg, "no") != 0) {
                    snprintf(err, errsz, "line %d: expected yes or no", line_no);
                    ok = false;
                    break;
                }
                done = event_engine_item_answer(en, yes);
            } else {
                int action = strcmp(arg, "up") == 0 ? 1
                           : strcmp(arg, "down") == 0 ? -1
                           : strcmp(arg, "land") == 0 ? 0 : 2;
                if (action == 2) {
                    snprintf(err, errsz, "line %d: expected up, down or land",
                             line_no);
                    ok = false;
                    break;
                }
                done = event_engine_fly(en, action);
            }
            if (!done) {
                snprintf(err, errsz, "line %d: no such panel is open", line_no);
                ok = false;
                break;
            }
            drain(en);
            state_compute_checksums(&game.state);
            if (en->status_text[0])
                printf("%s %s: %s\n", text, arg, en->status_text);
            en->status_text[0] = '\0';
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
    event_engine_shutdown(en);
    if (read_error) {
        if (err && errsz)
            snprintf(err, errsz, "read error in %s", script_path);
        return false;
    }
    return ok;
}
