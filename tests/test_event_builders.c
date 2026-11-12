/* Builder tests.
 *
 * Part 1 always runs: hand-written expectations taken from the native oracle
 * traces documented in docs/re/16-event-pilot.md, plus fail-closed checks.
 *
 * Part 2 is golden-vector parity: every .vec file exported by
 * docs/re/tools/export_event_vectors.py whose handler has a builder must
 * reproduce the command words, the final state, map and auxiliaries
 * exactly.  Vectors come from the user's own TSW.exe and are not committed;
 * the directory is argv[2], $TSW_EVENT_VECTORS, or
 * $XDG_CACHE_HOME/tsw/vectors (~/.cache/tsw/vectors).  Missing vectors skip
 * part 2 only.
 */
#define _POSIX_C_SOURCE 200809L

#include "event_builders.h"
#include "event_vm.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

typedef struct {
    StateBlock state;
    MapBlock map;
    EventVm vm;
    EventAux aux;
} Fixture;

static void make(Fixture *f, int floor, int x, int y)
{
    memset(f, 0, sizeof(*f));
    f->state.raw[ST_LIFE] = 1000;
    f->state.raw[ST_ATTACK] = 100;
    f->state.raw[ST_DEFENSE] = 100;
    f->state.raw[ST_FLOOR] = floor;
    f->state.raw[ST_X] = x;
    f->state.raw[ST_Y] = y;
    f->state.raw[ST_FACING] = 4;
    event_vm_init(&f->vm, &f->state, &f->map);
    f->aux.v[EVAUX_DIRECTION] = 1;
}

static bool cmd_is(const EventVm *vm, int i, int op, int a1, int a2)
{
    const EventCommand *c = &vm->commands[i];
    return c->opcode == op && c->arg1 == a1 && c->arg2 == a2;
}

static void unit_tests(EventBuilders *b)
{
    Fixture f;

    /* roujin floor 2, first visit (oracle trace: 6 commands). */
    make(&f, 2, 2, 7);
    CHECK(event_builders_run(b, "roujin", &f.vm, &f.aux));
    CHECK(f.vm.count == 6);
    CHECK(cmd_is(&f.vm, 1, 0, 0, 0) && cmd_is(&f.vm, 2, 2, 0, 0));
    CHECK(cmd_is(&f.vm, 4, 1, 2, 0));
    CHECK(f.state.raw[ST_ATTACK] == 103 && f.state.raw[ST_DEFENSE] == 103);
    CHECK(f.state.raw[0x62] == 1 && f.state.raw[0x42] == 1);
    CHECK(f.aux.v[EVAUX_MESSAGE_SELECTOR] == 0x19);
    CHECK(f.aux.v[EVAUX_EVENT_SELECTOR] == 0x42);

    /* roujin floor 2, repeat: no commands, repeat dialogue selector. */
    make(&f, 2, 2, 7);
    f.state.raw[0x62] = 1;
    f.state.raw[0x42] = 1;
    CHECK(event_builders_run(b, "roujin", &f.vm, &f.aux));
    CHECK(f.vm.count == 0);
    CHECK(f.aux.v[EVAUX_MESSAGE_SELECTOR] == 0x18);

    /* roujin floor 3 with the Orb of Wisdom: flag value 2, Orb of the Hero. */
    make(&f, 3, 9, 5);
    f.state.raw[ST_SPECIAL_FIRST + 1] = 1;
    CHECK(event_builders_run(b, "roujin", &f.vm, &f.aux));
    CHECK(f.state.raw[0x43] == 2);
    CHECK(f.state.raw[ST_SPECIAL_FIRST + 0] == 2);
    CHECK(f.vm.count == 6 && cmd_is(&f.vm, 6, 2, 0x0b, 0));

    /* Floor-3 trigger from an adjacent tile: the player steps onto (4,8),
     * the tile is cleared and 41 commands are queued. */
    make(&f, 3, 4, 9);
    f.map.bytes[map_floor_offset(3) + 92] = 0x07;
    f.aux.v[EVAUX_TARGET_INDEX] = 92;
    CHECK(event_builders_run(b, "handan", &f.vm, &f.aux));
    CHECK(f.state.raw[ST_X] == 4 && f.state.raw[ST_Y] == 8);
    CHECK(map_tile(&f.map, 3, 92) == 0x06);
    CHECK(f.vm.count == 41);
    CHECK(cmd_is(&f.vm, 1, 0x0a, 10, 0));
    CHECK(cmd_is(&f.vm, 7, 0x09, 0x46, 0xb6));
    CHECK(cmd_is(&f.vm, 41, 0x01, 7, 0));
    CHECK(f.aux.v[EVAUX_EVENT_SPRITE] == 0x46);

    /* Revisiting the cleared tile is a plain walk: no commands. */
    f.vm.count = 0;
    f.state.raw[ST_X] = 4;
    f.state.raw[ST_Y] = 9;
    CHECK(event_builders_run(b, "handan", &f.vm, &f.aux));
    CHECK(f.vm.count == 0);
    CHECK(f.state.raw[ST_Y] == 8);

    /* Fail closed: an unported handan case restores everything. */
    make(&f, 3, 4, 9);
    f.map.bytes[map_floor_offset(3) + 92] = 0x01;   /* yellow door */
    f.aux.v[EVAUX_TARGET_INDEX] = 92;
    f.aux.v[EVAUX_EVENT_SELECTOR] = 5;
    event_vm_append(&f.vm, EVOP_NOP, 1, 2);
    CHECK(!event_builders_run(b, "handan", &f.vm, &f.aux));
    CHECK(strstr(b->err, "not ported") != NULL);
    CHECK(f.vm.count == 1 && cmd_is(&f.vm, 1, 0, 1, 2));
    CHECK(f.aux.v[EVAUX_EVENT_SELECTOR] == 5);

    /* Fail closed after side effects: magician next to the destination. */
    make(&f, 3, 4, 9);
    f.map.bytes[map_floor_offset(3) + 92] = 0x07;
    f.map.bytes[map_floor_offset(3) + 91] = 0x97;
    f.aux.v[EVAUX_TARGET_INDEX] = 92;
    CHECK(!event_builders_run(b, "handan", &f.vm, &f.aux));
    CHECK(f.state.raw[ST_Y] == 9);
    CHECK(map_tile(&f.map, 3, 92) == 0x07);
    CHECK(f.vm.count == 0);

    /* Builders drain cleanly through the VM. */
    make(&f, 2, 2, 7);
    CHECK(event_builders_run(b, "roujin", &f.vm, &f.aux));
    CHECK(event_vm_run(&f.vm, 100) == 6);
    CHECK(!event_vm_active(&f.vm));

    CHECK(!event_builders_run(b, "no_such_builder", &f.vm, &f.aux));
}

/* ------------------------------------------------------------------ */
/* golden vectors */

typedef struct {
    char case_id[64];
    char handler[64];
    StateBlock state;
    MapBlock map;
    EventAux aux;
    EventCommand *cmds;
    int ncmds;
    int cap;
    StateBlock final_state;
    MapBlock final_map;
    EventAux final_aux;
} Vector;

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool load_vector(const char *path, Vector *v)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        return false;
    memset(v, 0, sizeof(*v));
    size_t cap = 0;
    char *line = NULL;
    bool ok = true, ended = false;
    while (getline(&line, &cap, fp) > 0) {
        char key[16], name[64];
        long a, b2, c;
        if (sscanf(line, "%15s", key) != 1)
            continue;
        if (!strcmp(key, "case")) {
            sscanf(line, "case %63s", v->case_id);
        } else if (!strcmp(key, "handler")) {
            sscanf(line, "handler %63s", v->handler);
        } else if (!strcmp(key, "state") && sscanf(line, "state %ld %ld", &a, &b2) == 2) {
            if (a >= 0 && a < STATE_INT_COUNT) v->state.raw[a] = (int32_t)b2; else ok = false;
        } else if (!strcmp(key, "map")) {
            const char *h = line + 4;
            for (int i = 0; i < MAP_BLOCK_SIZE; i++) {
                int hi = hexval(h[2 * i]), lo = hexval(h[2 * i + 1]);
                if (hi < 0 || lo < 0) { ok = false; break; }
                v->map.bytes[i] = (uint8_t)(hi * 16 + lo);
            }
        } else if (!strcmp(key, "aux") && sscanf(line, "aux %63s %ld", name, &a) == 2) {
            int id = event_aux_find(name);
            if (id >= 0) v->aux.v[id] = (int32_t)a;
        } else if (!strcmp(key, "cmd") && sscanf(line, "cmd %ld %ld %ld", &a, &b2, &c) == 3) {
            if (v->ncmds == v->cap) {
                v->cap = v->cap ? v->cap * 2 : 64;
                v->cmds = realloc(v->cmds, (size_t)v->cap * sizeof(*v->cmds));
            }
            v->cmds[v->ncmds++] = (EventCommand){ (uint16_t)a, (uint16_t)b2, (uint16_t)c };
        } else if (!strcmp(key, "dstate") || !strcmp(key, "dmap") || !strcmp(key, "daux")) {
            /* applied after the initial snapshot is complete */
        } else if (!strcmp(key, "end")) {
            ended = true;
        }
    }
    /* second pass: deltas on top of the snapshot */
    v->final_state = v->state;
    v->final_map = v->map;
    v->final_aux = v->aux;
    rewind(fp);
    while (getline(&line, &cap, fp) > 0) {
        char name[64];
        long a, b2;
        if (sscanf(line, "dstate %ld %ld", &a, &b2) == 2 && a >= 0 && a < STATE_INT_COUNT)
            v->final_state.raw[a] = (int32_t)b2;
        else if (sscanf(line, "dmap %ld %ld", &a, &b2) == 2 && a >= 0 && a < MAP_BLOCK_SIZE)
            v->final_map.bytes[a] = (uint8_t)b2;
        else if (sscanf(line, "daux %63s %ld", name, &a) == 2) {
            int id = event_aux_find(name);
            if (id >= 0) v->final_aux.v[id] = (int32_t)a;
        }
    }
    free(line);
    fclose(fp);
    return ok && ended && v->case_id[0] && v->handler[0];
}

static bool run_vector(EventBuilders *b, const Vector *v)
{
    static StateBlock state;
    static MapBlock map;
    static EventVm vm;
    EventAux aux = v->aux;
    state = v->state;
    map = v->map;
    event_vm_init(&vm, &state, &map);
    if (!event_builders_run(b, v->handler, &vm, &aux)) {
        fprintf(stderr, "  %s: builder failed: %s\n", v->case_id, b->err);
        return false;
    }
    bool ok = true;
    if (vm.count != v->ncmds) {
        fprintf(stderr, "  %s: %d commands, oracle %d\n", v->case_id, vm.count, v->ncmds);
        ok = false;
    }
    int n = vm.count < v->ncmds ? vm.count : v->ncmds;
    for (int i = 0; i < n; i++) {
        const EventCommand *c = &vm.commands[i + 1], *e = &v->cmds[i];
        if (c->opcode != e->opcode || c->arg1 != e->arg1 || c->arg2 != e->arg2) {
            fprintf(stderr, "  %s: command %d = (%u,%u,%u), oracle (%u,%u,%u)\n",
                    v->case_id, i + 1, c->opcode, c->arg1, c->arg2,
                    e->opcode, e->arg1, e->arg2);
            ok = false;
            break;
        }
    }
    for (int i = 0; i < STATE_INT_COUNT; i++)
        if (state.raw[i] != v->final_state.raw[i]) {
            fprintf(stderr, "  %s: state[%d] = %d, oracle %d\n", v->case_id, i,
                    state.raw[i], v->final_state.raw[i]);
            ok = false;
        }
    for (int i = 0; i < MAP_BLOCK_SIZE; i++)
        if (map.bytes[i] != v->final_map.bytes[i]) {
            fprintf(stderr, "  %s: map[%d] = %u, oracle %u\n", v->case_id, i,
                    map.bytes[i], v->final_map.bytes[i]);
            ok = false;
        }
    for (int i = 0; i < EVAUX_COUNT; i++)
        if (aux.v[i] != v->final_aux.v[i]) {
            fprintf(stderr, "  %s: aux %s = %d, oracle %d\n", v->case_id,
                    event_aux_name(i), aux.v[i], v->final_aux.v[i]);
            ok = false;
        }
    return ok;
}

static const char *vector_dir(int argc, char **argv, char *buf, size_t bufsz)
{
    if (argc > 2 && argv[2][0])
        return argv[2];
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

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void parity_tests(EventBuilders *b, const char *dir)
{
    DIR *d = dir ? opendir(dir) : NULL;
    if (!d) {
        printf("test_event_builders: no golden vectors in %s; parity skipped\n",
               dir ? dir : "(unset)");
        return;
    }
    char *names[256];
    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) && count < 256) {
        size_t len = strlen(ent->d_name);
        if (len > 4 && !strcmp(ent->d_name + len - 4, ".vec"))
            names[count++] = strdup(ent->d_name);
    }
    closedir(d);
    qsort(names, (size_t)count, sizeof(names[0]), cmp_str);
    int passed = 0, skipped = 0, failed = 0;
    for (int i = 0; i < count; i++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        Vector *v = calloc(1, sizeof(*v));
        if (!load_vector(path, v)) {
            fprintf(stderr, "  %s: malformed vector\n", names[i]);
            failed++;
        } else if (!event_builders_has(b, v->handler)) {
            printf("  %-24s not ported (%s)\n", v->case_id, v->handler);
            skipped++;
        } else if (run_vector(b, v)) {
            printf("  %-24s ok (%d commands)\n", v->case_id, v->ncmds);
            passed++;
        } else {
            failed++;
        }
        free(v->cmds);
        free(v);
        free(names[i]);
    }
    printf("test_event_builders: parity %d ok, %d not ported, %d failed\n",
           passed, skipped, failed);
    failures += failed;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: test_event_builders <builders.lua> [vector-dir]\n");
        return 2;
    }
    EventBuilders b;
    char err[512];
    if (!event_builders_init(&b, argv[1], err, sizeof(err))) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    unit_tests(&b);
    char buf[1024];
    parity_tests(&b, vector_dir(argc, argv, buf, sizeof(buf)));
    event_builders_shutdown(&b);
    if (failures) {
        fprintf(stderr, "test_event_builders: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_event_builders: ok\n");
    return 0;
}
