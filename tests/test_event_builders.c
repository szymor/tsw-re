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
#include "event_engine.h"
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

    /* Fail closed: a builder that rejects its input restores everything
     * (taisen2 on a cell that is not a monster). */
    make(&f, 3, 4, 9);
    f.map.bytes[map_floor_offset(3) + 92] = 0x06;
    f.aux.v[EVAUX_TARGET_INDEX] = 92;
    f.aux.v[EVAUX_EVENT_SELECTOR] = 5;
    event_vm_append(&f.vm, EVOP_NOP, 1, 2);
    CHECK(!event_builders_run(b, "taisen2", &f.vm, &f.aux));
    CHECK(strstr(b->err, "not a monster") != NULL);
    CHECK(f.vm.count == 1 && cmd_is(&f.vm, 1, 0, 1, 2));
    CHECK(f.aux.v[EVAUX_EVENT_SELECTOR] == 5);

    /* A magician next to the destination strikes (idou's mazyutu1). */
    make(&f, 3, 4, 9);
    f.map.bytes[map_floor_offset(3) + 92] = 0x07;
    f.map.bytes[map_floor_offset(3) + 91] = 0x97;
    f.aux.v[EVAUX_TARGET_INDEX] = 92;
    CHECK(event_builders_run(b, "handan", &f.vm, &f.aux));
    CHECK(f.state.raw[ST_Y] == 8);
    bool life_hit = false;
    for (int i = 1; i <= f.vm.count; i++)
        if (f.vm.commands[i].opcode == EVOP_ADD_LIFE &&
            f.vm.commands[i].arg1 == (uint16_t)-200)
            life_hit = true;
    CHECK(life_hit);

    /* Fail closed after side effects: openning2 appends its whole
     * timeline before it rejects an invalid facing. */
    make(&f, 3, 4, 9);
    f.state.raw[0x58] = 0;
    event_vm_append(&f.vm, EVOP_NOP, 1, 2);
    CHECK(!event_builders_run(b, "openning2", &f.vm, &f.aux));
    CHECK(strstr(b->err, "facing") != NULL);
    CHECK(f.vm.count == 1 && cmd_is(&f.vm, 1, 0, 1, 2));

    /* Builders drain cleanly through the VM. */
    make(&f, 2, 2, 7);
    CHECK(event_builders_run(b, "roujin", &f.vm, &f.aux));
    CHECK(event_vm_run(&f.vm, 100) == 6);
    CHECK(!event_vm_active(&f.vm));

    CHECK(!event_builders_run(b, "no_such_builder", &f.vm, &f.aux));
}

/* ------------------------------------------------------------------ */
/* golden vectors */

#include "vectors.h"

/* The vector's ranking file, as BlockRead returns it. */
static bool vector_ranking(void *user, int area, int32_t out[72])
{
    const Vector *v = user;
    (void)area;
    if (v->has_rankfile)
        memcpy(out, v->rankfile, 72 * sizeof(int32_t));
    else
        memset(out, 0, 72 * sizeof(int32_t));
    return true;
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
    b->ranking_fn = vector_ranking;
    b->ranking_user = (void *)v;
    event_vm_load(&vm, v->pre, v->npre);
    vm.count = v->precount;
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

/* End-to-end parity with the execution oracle: run the builder through the
 * real engine, let Timer2 consume the timeline, confirm every dialogue the
 * moment it opens, and compare the confirmed sub-event sequence and the
 * final state and map with the oracle's drain (16-event-pilot.md 8). */
static bool run_drain(EventEngine *e, const Vector *v)
{
    static StateBlock state;
    static MapBlock map;
    state = v->state;
    map = v->map;
    event_engine_cancel(e);
    event_engine_bind(e, &state, &map);
    /* The oracle runs no syokidata2: Zeno's entry is the shipped one. */
    e->vm.zeno[0] = 8000; e->vm.zeno[1] = 5000; e->vm.zeno[2] = 1000;
    e->aux = v->aux;
    e->builders.ranking_fn = vector_ranking;
    e->builders.ranking_user = (void *)v;
    event_vm_load(&e->vm, v->pre, v->npre);
    e->vm.count = v->precount;
    if (!event_builders_run(&e->builders, v->handler, &e->vm, &e->aux)) {
        fprintf(stderr, "  %s: drain builder failed: %s\n", v->case_id,
                e->builders.err);
        return false;
    }
    /* moncheck is a splice inside the battle timeline, whose Timer2 is
     * already running (it never calls timer2on itself). */
    if (!strcmp(v->handler, "moncheck") && e->vm.count > 0)
        e->timer2 = true;
    int seen[64], n = 0;
    uint64_t t = 1000;
    bool ok = true;
    for (int i = 0; i < 40000 && event_engine_active(e); i++) {
        t += 50;
        event_engine_update(e, t);
        /* A Yes/No question is left unanswered (the oracle's
         * "choice_pending"); the timeline waits there. */
        if (((e->dialog_open && e->dialog_choice) || e->altar_open ||
             e->item_confirm || e->fly_open || e->wisdom_open) &&
            e->vm.count == 0)
            break;
        if (e->ending_wait)             /* the oracle's timer_off stop */
            break;
        if (getenv("DBG")) fprintf(stderr, "i=%d count=%d t2=%d dp=%d do=%d dc=%d res=%d\n", i, e->vm.count, e->timer2, e->death_prompt, e->dialog_open, e->dialog_choice, e->resets);
        if (e->death_prompt)            /* MessageDlg: the witness's answer */
            event_engine_death_answer(e, v->message_dlg == 6);
        /* The ranking form (the oracle's ShowModal returns at once): no
         * name, then close; ShowMessage: OK. */
        if (e->ranking_name_entry)
            event_engine_ranking_name(e, NULL, false);
        if (e->ranking_open)
            event_engine_close_ranking(e);
        if (e->message_open)
            event_engine_close_message(e);
        if (e->dialog_open && !e->dialog_choice) {
            if (n < 64)
                seen[n++] = e->dialog_id;
            event_engine_confirm(e);
        }
        if (e->unported) {
            fprintf(stderr, "  %s: drain reached an unported sub-event\n",
                    v->case_id);
            return false;
        }
    }
    /* The item / flight / Orb of Wisdom panels wait for a button like the
     * oracle's modal_idle stop. */
    if (event_engine_active(e) && !(e->dialog_open && e->dialog_choice) &&
        !e->altar_open && !e->item_confirm && !e->fly_open &&
        !e->wisdom_open && !e->ending_wait) {
        fprintf(stderr, "  %s: drain did not finish\n", v->case_id);
        ok = false;
    }
    if (n != v->ndialogues) {
        fprintf(stderr, "  %s: %d dialogues, oracle %d\n", v->case_id, n,
                v->ndialogues);
        ok = false;
    }
    for (int i = 0; i < n && i < v->ndialogues; i++)
        if (seen[i] != v->dialogues[i]) {
            fprintf(stderr, "  %s: dialogue %d = %d, oracle %d\n", v->case_id,
                    i, seen[i], v->dialogues[i]);
            ok = false;
        }
    for (int i = 0; i < STATE_INT_COUNT; i++)
        if (state.raw[i] != v->drain_state.raw[i]) {
            fprintf(stderr, "  %s: drained state[%d] = %d, oracle %d\n",
                    v->case_id, i, state.raw[i], v->drain_state.raw[i]);
            ok = false;
        }
    for (int i = 0; i < MAP_BLOCK_SIZE; i++)
        if (map.bytes[i] != v->drain_map.bytes[i]) {
            fprintf(stderr, "  %s: drained map[%d] = %u, oracle %u\n",
                    v->case_id, i, map.bytes[i], v->drain_map.bytes[i]);
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

/* An action vector's handan case is ported when the builder runs it
 * without reaching the "handan raw" fail-closed path. */
static bool handan_ported(EventBuilders *b, const Vector *v)
{
    static StateBlock state;
    static MapBlock map;
    static EventVm vm;
    EventAux aux = v->aux;
    state = v->state;
    map = v->map;
    event_vm_init(&vm, &state, &map);
    if (event_builders_run(b, v->handler, &vm, &aux))
        return true;
    return strstr(b->err, "handan raw") == NULL;
}

static void parity_tests(EventBuilders *b, EventEngine *engine,
                         const char *dir)
{
    DIR *d = dir ? opendir(dir) : NULL;
    if (!d) {
        printf("test_event_builders: no golden vectors in %s; parity skipped\n",
               dir ? dir : "(unset)");
        return;
    }
    char **names = NULL;
    int count = 0, cap = 0;
    struct dirent *ent;
    while ((ent = readdir(d))) {
        size_t len = strlen(ent->d_name);
        if (len <= 4 || strcmp(ent->d_name + len - 4, ".vec"))
            continue;
        if (count == cap) {
            cap = cap ? cap * 2 : 256;
            names = realloc(names, (size_t)cap * sizeof(*names));
        }
        names[count++] = strdup(ent->d_name);
    }
    closedir(d);
    qsort(names, (size_t)count, sizeof(names[0]), cmp_str);
    int passed = 0, skipped = 0, failed = 0;
    int drained = 0, drain_failed = 0, no_drain = 0;
    for (int i = 0; i < count; i++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        Vector *v = calloc(1, sizeof(*v));
        if (!load_vector(path, v)) {
            fprintf(stderr, "  %s: malformed vector\n", names[i]);
            failed++;
        } else if (strncmp(v->case_id, "action_", 7) == 0 &&
                   !handan_ported(b, v)) {
            /* Ordinary map actions whose handan case is not a builder yet:
             * src/game.c, checked by test_game_parity. */
            printf("  %-24s C movement code (test_game_parity)\n", v->case_id);
            skipped++;
        } else if (!event_builders_has(b, v->handler)) {
            printf("  %-24s not ported (%s)\n", v->case_id, v->handler);
            skipped++;
        } else if (run_vector(b, v)) {
            printf("  %-24s ok (%d commands)\n", v->case_id, v->ncmds);
            passed++;
            if (!v->has_drain) {
                no_drain++;
            } else if (engine && run_drain(engine, v)) {
                printf("  %-24s drain ok (%d dialogues)\n", v->case_id,
                       v->ndialogues);
                drained++;
            } else {
                drain_failed++;
            }
        } else {
            failed++;
        }
        free(v->cmds);
        free(v);
        free(names[i]);
    }
    free(names);
    printf("test_event_builders: parity %d ok, %d not ported, %d failed\n",
           passed, skipped, failed);
    printf("test_event_builders: drain parity %d ok, %d without drain, "
           "%d failed\n", drained, no_drain, drain_failed);
    failures += failed + drain_failed;
}

static void test_log(void *user, const char *message)
{
    (void)user;
    fprintf(stderr, "    log: %s\n", message);
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
    static EventEngine engine;
    if (getenv("TSW_TEST_LOG")) {          /* engine log to stderr */
        engine.log_fn = test_log;
        engine.log_user = NULL;
    }
    bool have_engine = event_engine_init(&engine, argv[1], err, sizeof(err));
    if (!have_engine)
        fprintf(stderr, "engine: %s\n", err);
    /* op 0x16 (gameover) rewrites the initial map: data/maps/initial.bin
     * (extracted, gitignored).  Without it the reset leaves the map. */
    static MapBlock initial;
    FILE *mf = fopen(TSW_TEST_DATA_DIR "/maps/initial.bin", "rb");
    if (mf) {
        if (fread(initial.bytes, 1, MAP_BLOCK_SIZE, mf) == MAP_BLOCK_SIZE)
            event_engine_set_initial_map(&engine, &initial);
        fclose(mf);
    }
    parity_tests(&b, have_engine ? &engine : NULL,
                 vector_dir(argc, argv, buf, sizeof(buf)));
    event_engine_shutdown(&engine);
    event_builders_shutdown(&b);
    if (failures) {
        fprintf(stderr, "test_event_builders: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_event_builders: ok\n");
    return 0;
}
