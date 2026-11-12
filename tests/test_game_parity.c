/* Differential test of the C movement code (src/game.c) against the
 * original's handan (0x444490) for ordinary map actions: doors, keys,
 * items, stairs.  The reference is the execution oracle's drained result
 * (export_event_vectors.py, witnesses action_*): the original's handler runs
 * natively and its timeline is drained, so the door, item and stairs
 * commands have written the map and state.  game_move() must arrive at the
 * same state block and map.
 *
 * The vectors are exported locally from the user's TSW.exe and not
 * committed; without them the test is skipped. */
#include "game.h"
#include "vectors.h"

#include <dirent.h>

static int failures;
static int checked, skipped_cases;

/* The save checksums: the remake keeps them current after every move, the
 * original computes them only when saving. */
static bool ignored_state(int i)
{
    return i == ST_CHECKSUM_SUM || i == ST_CHECKSUM_ALT;
}

static const char *vector_dir(int argc, char **argv, char *buf, size_t bufsz)
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

/* Known, documented divergences (see the comment in is_walkable). */
static bool known_divergence(const char *case_id)
{
    return !strcmp(case_id, "action_raw_0e");
}

static void check_vector(const Vector *v)
{
    if (known_divergence(v->case_id)) {
        printf("  %-28s known divergence (Snow Crystal ice)\n", v->case_id);
        return;
    }
    if (!v->has_drain) {
        skipped_cases++;
        return;
    }
    static Game g;
    game_init(&g);
    g.map = v->map;
    g.state = v->state;
    g.has_state = true;
    int x = v->state.raw[ST_X], y = v->state.raw[ST_Y];
    int t = v->aux.v[EVAUX_TARGET_INDEX];
    int dx = t % MAP_TILE_COLS - x, dy = t / MAP_TILE_COLS - y;
    char msg[256];
    GameMoveResult r = game_move(&g, dx, dy, msg, sizeof(msg));
    bool ok = true;
    for (int i = 0; i < STATE_INT_COUNT; i++) {
        if (ignored_state(i) || g.state.raw[i] == v->drain_state.raw[i])
            continue;
        fprintf(stderr, "  %s: state[%d] = %d, original %d\n", v->case_id, i,
                g.state.raw[i], v->drain_state.raw[i]);
        ok = false;
    }
    for (int i = 0; i < MAP_BLOCK_SIZE; i++) {
        if (g.map.bytes[i] == v->drain_map.bytes[i])
            continue;
        fprintf(stderr, "  %s: map[%d] = %u, original %u\n", v->case_id, i,
                g.map.bytes[i], v->drain_map.bytes[i]);
        ok = false;
    }
    checked++;
    if (!ok) {
        fprintf(stderr, "  %s: game_move -> %d (%s)\n", v->case_id, (int)r, msg);
        failures++;
    } else {
        printf("  %-28s ok\n", v->case_id);
    }
}

int main(int argc, char **argv)
{
    char buf[1024];
    const char *dir = vector_dir(argc, argv, buf, sizeof(buf));
    DIR *d = dir ? opendir(dir) : NULL;
    if (!d) {
        printf("test_game_parity: no golden vectors in %s; skipped\n",
               dir ? dir : "(unset)");
        return 0;
    }
    struct dirent *de;
    static Vector v;
    while ((de = readdir(d)) != NULL) {
        size_t n = strlen(de->d_name);
        if (strncmp(de->d_name, "action_", 7) != 0 || n < 5 ||
            strcmp(de->d_name + n - 4, ".vec") != 0)
            continue;
        char path[2048];
        snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
        if (!load_vector(path, &v)) {
            fprintf(stderr, "  %s: unreadable\n", de->d_name);
            failures++;
            continue;
        }
        check_vector(&v);
        free(v.cmds);
    }
    closedir(d);
    printf("test_game_parity: %d checked, %d without drain, %d failed\n",
           checked, skipped_cases, failures);
    return failures ? 1 : 0;
}
