/* The remake against data the original TSW.exe produced, exported beside
 * the event vectors by docs/re/tools/export_event_vectors.py:
 *
 *   monsters.tbl  the monster table (0x489910) as the executable holds it;
 *   *.mapdraw     mhyouji (0x442c38) drawing a floor: the ImageList index
 *                 of every cell in both map buffers (frames A and B);
 *   *.sav         savework (0x47eadc) writing a game, and loadwork
 *                 (0x47e7d4) reading the file back, then with one byte of
 *                 the state changed.
 *
 * Skipped when nothing was exported (the vectors are not committed). */

#include "battle.h"
#include "event_engine.h"
#include "fileio.h"
#include "game.h"
#include "map.h"
#include "save.h"
#include "state.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* mhyouji's cell size: the display object's +0x2c/+0x30 (32 in the
 * oracle, the original's tile size). */
#define CELL_PX 32

static int failures;
static int checked;

#define FAIL(...)                                                            \
    do {                                                                     \
        fprintf(stderr, "  " __VA_ARGS__);                                   \
        fputc('\n', stderr);                                                 \
        failures++;                                                          \
    } while (0)

/* --- input ------------------------------------------------------------ */

static const char *data_dir(int argc, char **argv, char *buf, size_t bufsz)
{
    if (argc > 1 && argv[1][0])
        return argv[1];
    const char *env = getenv("TSW_EVENT_VECTORS");
    if (env && *env)
        return env;
    const char *cache = getenv("XDG_CACHE_HOME");
    if (cache && *cache) {
        snprintf(buf, bufsz, "%s/tsw/vectors", cache);
        return buf;
    }
    const char *home = getenv("HOME");
    if (!home)
        return NULL;
    snprintf(buf, bufsz, "%s/.cache/tsw/vectors", home);
    return buf;
}

/* The words of a line: `key` and up to 8 more, NUL-terminated in place. */
typedef struct {
    char *word[9];
    int n;
} Words;

static Words split(char *line)
{
    Words w = { { 0 }, 0 };
    char *p = line;
    while (*p && w.n < 9) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        w.word[w.n++] = p;
        while (*p && *p != ' ')
            p++;
        if (*p)
            *p++ = '\0';
    }
    return w;
}

/* Hex into bytes; the number of bytes, or -1 on a bad digit. */
static long unhex(const char *h, uint8_t *out, size_t max)
{
    size_t n = 0;
    for (; h[0] && h[1] && n < max; h += 2) {
        int hi = h[0] <= '9' ? h[0] - '0' : (h[0] | 0x20) - 'a' + 10;
        int lo = h[1] <= '9' ? h[1] - '0' : (h[1] | 0x20) - 'a' + 10;
        if (hi < 0 || hi > 15 || lo < 0 || lo > 15)
            return -1;
        out[n++] = (uint8_t)(hi * 16 + lo);
    }
    return (long)n;
}

/* Calls fn(user, line) for every line of `path`; false when unreadable. */
static bool each_line(const char *path, void (*fn)(void *, char *),
                      void *user)
{
    size_t len = 0;
    char *text = file_load(path, &len);
    if (!text)
        return false;
    char *p = text;
    while (*p) {
        char *e = strchr(p, '\n');
        if (e)
            *e = '\0';
        if (*p)
            fn(user, p);
        if (!e)
            break;
        p = e + 1;
    }
    file_free(text);
    return true;
}

/* --- the monster table ------------------------------------------------- */

static void monster_line(void *user, char *line)
{
    (void)user;
    Words w = split(line);
    if (w.n != 6 || strcmp(w.word[0], "monster"))
        return;
    int i = atoi(w.word[1]);
    int v[4] = { atoi(w.word[2]), atoi(w.word[3]), atoi(w.word[4]),
                 atoi(w.word[5]) };
    checked++;
    if (i < 0 || i >= MONSTER_COUNT) {
        FAIL("monster %d: not in the remake's table", i);
        return;
    }
    const MonsterDef *m = &MONSTER_TABLE[i];
    if (m->hp != v[0] || m->attack != v[1] || m->defense != v[2] ||
        m->gold != v[3])
        FAIL("monster %d (%s): %d/%d/%d/%d, original %d/%d/%d/%d", i,
             m->name, m->hp, m->attack, m->defense, m->gold, v[0], v[1],
             v[2], v[3]);
}

/* --- map drawing ------------------------------------------------------- */

typedef struct {
    char name[64];
    int state74;
    uint8_t cells[MAP_TILES];
    bool have_cells;
    int ndraws;
    int x[256], y[256], index[256];
} MapDraw;

static void mapdraw_line(void *user, char *line)
{
    MapDraw *d = user;
    Words w = split(line);
    if (w.n == 2 && !strcmp(w.word[0], "case"))
        snprintf(d->name, sizeof(d->name), "%s", w.word[1]);
    else if (w.n == 2 && !strcmp(w.word[0], "state74"))
        d->state74 = atoi(w.word[1]);
    else if (w.n == 2 && !strcmp(w.word[0], "cells"))
        d->have_cells = unhex(w.word[1], d->cells, MAP_TILES) == MAP_TILES;
    else if (w.n == 4 && !strcmp(w.word[0], "draw") && d->ndraws < 256) {
        d->x[d->ndraws] = atoi(w.word[1]);
        d->y[d->ndraws] = atoi(w.word[2]);
        d->index[d->ndraws] = atoi(w.word[3]);
        d->ndraws++;
    }
}

/* mhyouji draws every cell into buffer A, then B: ImageList index
 * value - 1 in A, value - 1 + imagework's offset in B. */
static void check_mapdraw(const char *path)
{
    static MapDraw d;
    memset(&d, 0, sizeof(d));
    if (!each_line(path, mapdraw_line, &d) || !d.have_cells) {
        FAIL("%s: unreadable", path);
        return;
    }
    checked++;
    if (d.ndraws != 2 * MAP_TILES) {
        FAIL("%s: %d draws, expected %d", d.name, d.ndraws, 2 * MAP_TILES);
        return;
    }
    bool seen[MAP_TILES] = { false };
    for (int k = 0; k < d.ndraws; k += 2) {
        int cell = (d.y[k] / CELL_PX) * MAP_TILE_COLS + d.x[k] / CELL_PX;
        if (d.x[k] % CELL_PX || d.y[k] % CELL_PX || cell < 0 ||
            cell >= MAP_TILES || seen[cell] ||
            d.x[k + 1] != d.x[k] || d.y[k + 1] != d.y[k]) {
            FAIL("%s: draw %d at (%d,%d) is not a new cell", d.name, k,
                 d.x[k], d.y[k]);
            return;
        }
        seen[cell] = true;
        int raw = d.cells[cell];
        int a = event_engine_map_tile(raw, false, d.state74) - 1;
        int b = event_engine_map_tile(raw, true, d.state74) - 1;
        if (a != d.index[k] || b != d.index[k + 1])
            FAIL("%s: cell %d value 0x%02x draws %d/%d, original %d/%d",
                 d.name, cell, raw, a, b, d.index[k], d.index[k + 1]);
    }
}

/* --- saves ------------------------------------------------------------- */

typedef struct {
    char name[64];
    uint8_t game[SAVE_FILE_SIZE];
    uint8_t saved[SAVE_FILE_SIZE];
    long game_len, saved_len;
    /* [0] the saved file read back, [1] the same with a byte changed. */
    uint8_t load[2][SAVE_FILE_SIZE];
    long load_len[2];
    int accepted[2];
    uint8_t state[2][STATE_BLOCK_SIZE];
    uint8_t map[2][MAP_BLOCK_SIZE];
} SaveCase;

static void save_line(void *user, char *line)
{
    SaveCase *c = user;
    Words w = split(line);
    if (w.n != 2)
        return;
    const char *k = w.word[0], *v = w.word[1];
    int bad = strncmp(k, "load_bad", 8) == 0;
    const char *tail = k + (bad ? 8 : 4);
    if (!strcmp(k, "case"))
        snprintf(c->name, sizeof(c->name), "%s", v);
    else if (!strcmp(k, "game"))
        c->game_len = unhex(v, c->game, sizeof(c->game));
    else if (!strcmp(k, "saved"))
        c->saved_len = unhex(v, c->saved, sizeof(c->saved));
    else if (strncmp(k, "load", 4))
        return;
    else if (!*tail)
        c->load_len[bad] = unhex(v, c->load[bad], sizeof(c->load[0]));
    else if (!strcmp(tail, "_accepted"))
        c->accepted[bad] = atoi(v);
    else if (!strcmp(tail, "_state"))
        unhex(v, c->state[bad], STATE_BLOCK_SIZE);
    else if (!strcmp(tail, "_map"))
        unhex(v, c->map[bad], MAP_BLOCK_SIZE);
}

static void check_save(const char *path)
{
    static SaveCase c;
    memset(&c, 0, sizeof(c));
    if (!each_line(path, save_line, &c) || c.game_len != SAVE_FILE_SIZE ||
        c.saved_len != SAVE_FILE_SIZE) {
        FAIL("%s: unreadable", path);
        return;
    }
    checked++;
    /* Writing: the remake's file for the same game. */
    static SaveData game;
    memcpy(game.map.bytes, c.game, MAP_BLOCK_SIZE);
    if (!state_parse(c.game + MAP_BLOCK_SIZE, STATE_BLOCK_SIZE, &game.state,
                     NULL, 0)) {
        FAIL("%s: bad state", c.name);
        return;
    }
    const char *tmp = "test_original_save.dat";
    char err[256] = "";
    size_t len = 0;
    char *written = save_write_file(&game, tmp, err, sizeof(err))
        ? file_load(tmp, &len) : NULL;
    remove(tmp);
    if (!written || len != SAVE_FILE_SIZE) {
        FAIL("%s: the remake did not write it: %s", c.name, err);
    } else {
        for (size_t i = 0; i < len; i++)
            if ((uint8_t)written[i] != c.saved[i]) {
                FAIL("%s: byte %zu written 0x%02x, original 0x%02x", c.name,
                     i, (uint8_t)written[i], c.saved[i]);
                break;
            }
    }
    file_free(written);
    /* Reading: the same verdict, and the same game when accepted.  The
     * fairy's save0.dat stores no checksums; the remake reads it with its
     * checkpoint profile (Load's "Checkpoint" row). */
    bool checkpoint = strstr(c.name, "slot0") != NULL;
    for (int bad = 0; bad < 2; bad++) {
        static SaveData got;
        bool ok = save_parse_bytes_profile(c.load[bad],
                                           (size_t)c.load_len[bad], &got,
                                           checkpoint, NULL, 0);
        if (ok != (c.accepted[bad] != 0)) {
            FAIL("%s%s: remake %s it, original %s", c.name,
                 bad ? " (a byte changed)" : "", ok ? "accepts" : "rejects",
                 c.accepted[bad] ? "accepts" : "rejects");
            continue;
        }
        if (!ok)
            continue;
        /* The game as the remake loads it (game_load), against the
         * original's after loadwork; the remake also recomputes the
         * checksums, which the original only does when saving. */
        static Game loaded;
        game_init(&loaded);
        game_load(&loaded, &got);
        StateBlock want;
        state_parse(c.state[bad], STATE_BLOCK_SIZE, &want, NULL, 0);
        for (int i = 0; i < STATE_INT_COUNT; i++)
            if (i != ST_CHECKSUM_SUM && i != ST_CHECKSUM_ALT &&
                loaded.state.raw[i] != want.raw[i])
                FAIL("%s: loaded state[0x%x] = %d, original %d", c.name, i,
                     loaded.state.raw[i], want.raw[i]);
        if (memcmp(loaded.map.bytes, c.map[bad], MAP_BLOCK_SIZE))
            FAIL("%s: loaded map differs", c.name);
    }
}

/* --- the folder -------------------------------------------------------- */

typedef struct {
    char **names;
    int count, cap;
} NameList;

static SDL_EnumerationResult SDLCALL add_name(void *user, const char *dir,
                                              const char *name)
{
    (void)dir;
    NameList *l = user;
    size_t len = strlen(name);
    if (l->count == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 128;
        l->names = realloc(l->names, (size_t)l->cap * sizeof(*l->names));
    }
    l->names[l->count] = malloc(len + 1);
    memcpy(l->names[l->count++], name, len + 1);
    return SDL_ENUM_CONTINUE;
}

static int cmp_name(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static bool ends_with(const char *s, const char *tail)
{
    size_t n = strlen(s), m = strlen(tail);
    return n > m && !strcmp(s + n - m, tail);
}

int main(int argc, char **argv)
{
    char buf[1024];
    const char *dir = data_dir(argc, argv, buf, sizeof(buf));
    char path[2048];
    snprintf(path, sizeof(path), "%s/monsters.tbl", dir ? dir : ".");
    if (!dir || !path_is_file(path)) {
        printf("test_original: nothing exported in %s; skipped\n",
               dir ? dir : "(unset)");
        return 0;
    }
    int before = checked;
    each_line(path, monster_line, NULL);
    if (checked - before != MONSTER_COUNT)
        FAIL("monsters.tbl: %d monsters, the remake has %d", checked - before,
             MONSTER_COUNT);
    int monsters = checked - before;

    NameList l = { 0 };
    SDL_EnumerateDirectory(dir, add_name, &l);
    qsort(l.names, (size_t)l.count, sizeof(l.names[0]), cmp_name);
    int maps = 0, saves = 0;
    for (int i = 0; i < l.count; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, l.names[i]);
        if (ends_with(l.names[i], ".mapdraw")) {
            check_mapdraw(path);
            maps++;
        } else if (ends_with(l.names[i], ".sav")) {
            check_save(path);
            saves++;
        }
        free(l.names[i]);
    }
    free(l.names);
    printf("test_original: %d monsters, %d map drawings, %d saves; "
           "%d failure(s)\n", monsters, maps, saves, failures);
    if (failures)
        return 1;
    printf("test_original: ok\n");
    return 0;
}
