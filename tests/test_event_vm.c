/* Unit tests for src/event_vm.c, ported from docs/re/tools/test_event_vm.py. */
#include "event_vm.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

#define MAX_LOG 64

typedef struct {
    EventVmLog e[MAX_LOG];
    int n;
} Log;

typedef struct {
    StateBlock state;
    MapBlock map;
    EventVm vm;
    Log log;
} Fixture;

static void observer(EventVm *vm, const EventVmLog *entry, void *user)
{
    (void)vm;
    Log *log = user;
    if (log->n < MAX_LOG)
        log->e[log->n++] = *entry;
}

static void default_state(StateBlock *s)
{
    memset(s, 0, sizeof(*s));
    s->raw[ST_LIFE] = 1000;
    s->raw[ST_ATTACK] = 100;
    s->raw[ST_DEFENSE] = 100;
    s->raw[ST_FLOOR] = 1;
    s->raw[ST_FLOOR_LIMIT] = 1;
    s->raw[ST_X] = 5;
    s->raw[ST_Y] = 10;
    s->raw[ST_WEAPON_LEVEL] = 5;
    s->raw[ST_SHIELD_LEVEL] = 5;
    s->raw[ST_FACING] = 4;
    s->raw[ST_MAGIC] = -4;
    s->raw[ST_VERSION] = 12;
}

static void make(Fixture *f)
{
    memset(f, 0, sizeof(*f));
    default_state(&f->state);
    f->map.bytes[0] = 0x00;
    f->map.bytes[1] = 0x01;
    event_vm_init(&f->vm, &f->state, &f->map);
    f->vm.observer = observer;
    f->vm.observer_user = &f->log;
    f->vm.monster = (EventMonster){ 100, 20, 10, 5 };
    f->vm.monster_hp = 100;
    f->vm.monster_index = 0;
}

static void load(Fixture *f, const EventCommand *cmds, int n)
{
    CHECK(event_vm_load(&f->vm, cmds, n));
}

static int count_kind(const Log *log, EventVmLogKind kind)
{
    int n = 0;
    for (int i = 0; i < log->n; i++)
        n += log->e[i].kind == kind;
    return n;
}

static const EventVmLog *first_kind(const Log *log, EventVmLogKind kind)
{
    for (int i = 0; i < log->n; i++)
        if (log->e[i].kind == kind)
            return &log->e[i];
    return NULL;
}

static void test_tail_execution_order(void)
{
    Fixture f; make(&f);
    EventCommand c[] = {
        { EVOP_SET_STATE, ST_GOLD, 1 },
        { EVOP_SET_STATE, ST_GOLD, 2 },
        { EVOP_SET_STATE, ST_GOLD, 3 },
    };
    load(&f, c, 3);
    CHECK(f.vm.count == 3);
    event_vm_run(&f.vm, 100);
    CHECK(f.state.raw[ST_GOLD] == 1);
    CHECK(f.vm.count == 0);
}

static void test_nops(void)
{
    Fixture f; make(&f);
    EventCommand c[] = { { EVOP_NOP, 0, 0 }, { EVOP_NOP_12, 0, 0 } };
    load(&f, c, 2);
    CHECK(event_vm_run(&f.vm, 100) == 2);
    CHECK(f.log.n == 2 && count_kind(&f.log, EVLOG_NOP) == 2);
}

static void test_set_state_direct(void)
{
    Fixture f; make(&f);
    EventCommand c[] = { { EVOP_SET_STATE, ST_ATTACK, 42 } };
    load(&f, c, 1);
    event_vm_run(&f.vm, 100);
    CHECK(f.state.raw[ST_ATTACK] == 42);
}

static void test_set_tile(void)
{
    Fixture f; make(&f);
    f.state.raw[ST_FLOOR] = 7;
    EventCommand c[] = {
        { EVOP_SET_TILE, 10, 0x06 },
        { EVOP_SET_TILE_SILENT, 11, 0x07 },
    };
    load(&f, c, 2);
    event_vm_run(&f.vm, 100);
    CHECK(map_tile(&f.map, 7, 10) == 0x06);
    CHECK(map_tile(&f.map, 7, 11) == 0x07);
    CHECK(f.log.n == 2);
    CHECK(f.log.e[0].kind == EVLOG_SET_TILE && f.log.e[0].d == 1);
    CHECK(f.log.e[1].kind == EVLOG_SET_TILE && f.log.e[1].d == 0);
}

static void test_life_delta(void)
{
    Fixture f; make(&f);
    f.state.raw[ST_LIFE] = 100;
    f.state.raw[ST_DEFENSE] = 10;
    f.vm.monster.attack = 25;
    EventCommand c[] = { { EVOP_LIFE_DELTA, 0, 0 } };
    load(&f, c, 1);
    event_vm_run(&f.vm, 10);
    CHECK(f.state.raw[ST_LIFE] == 85);

    make(&f);
    f.state.raw[ST_LIFE] = 100;
    f.state.raw[ST_DEFENSE] = 50;
    f.vm.monster.attack = 10;
    load(&f, c, 1);
    event_vm_run(&f.vm, 10);
    CHECK(f.state.raw[ST_LIFE] == 100);
}

static void test_monster_delta(void)
{
    EventCommand c[] = { { EVOP_MONSTER_DELTA, 0, 0 } };
    Fixture f; make(&f);
    f.state.raw[ST_ATTACK] = 13;
    load(&f, c, 1);
    event_vm_run(&f.vm, 10);
    CHECK(f.vm.monster_hp == 97);

    make(&f);
    f.state.raw[ST_ATTACK] = 6;
    f.vm.monster_index = 12;
    f.state.raw[ST_SPECIAL_FIRST + EVM_SLOT_CROSS] = 1;
    load(&f, c, 1);
    event_vm_run(&f.vm, 10);
    CHECK(f.vm.monster_hp == 98);

    make(&f);
    f.state.raw[ST_ATTACK] = 3;
    load(&f, c, 1);
    event_vm_run(&f.vm, 10);
    CHECK(f.vm.monster_hp == 100);

    /* Dragon Slayer. */
    make(&f);
    f.state.raw[ST_ATTACK] = 6;
    f.vm.monster_index = 19;
    f.state.raw[ST_SPECIAL_FIRST + EVM_SLOT_DRAGON_SLAYER] = 1;
    load(&f, c, 1);
    event_vm_run(&f.vm, 10);
    CHECK(f.vm.monster_hp == 98);

    /* Difficulty multiplier. */
    make(&f);
    f.state.raw[ST_ATTACK] = 20;
    f.state.raw[ST_OPTION_A] = 1;
    load(&f, c, 1);
    event_vm_run(&f.vm, 10);
    CHECK(f.vm.monster_hp == 100);
    make(&f);
    f.state.raw[ST_ATTACK] = 22;
    f.state.raw[ST_OPTION_A] = 1;
    load(&f, c, 1);
    event_vm_run(&f.vm, 10);
    CHECK(f.vm.monster_hp == 98);
}

static void test_battle_hit_ignores_cross(void)
{
    Fixture f; make(&f);
    f.state.raw[ST_ATTACK] = 13;
    f.vm.monster_index = 12;
    f.state.raw[ST_SPECIAL_FIRST + EVM_SLOT_CROSS] = 1;
    EventCommand c[] = { { EVOP_BATTLE_HIT, 1, 2 } };
    load(&f, c, 1);
    event_vm_run(&f.vm, 10);
    CHECK(f.vm.monster_hp == 97);
}

static void test_lucky_gold_and_signed(void)
{
    Fixture f; make(&f);
    f.state.raw[ST_SPECIAL_FIRST + EVM_SLOT_LUCKY_GOLD] = 1;
    EventCommand c[] = { { EVOP_ADD_GOLD, 10, 0 } };
    load(&f, c, 1);
    event_vm_run(&f.vm, 10);
    CHECK(f.state.raw[ST_GOLD] == 20);

    make(&f);
    EventCommand d[] = { { EVOP_ADD_GOLD, 0xffff, 0 } };
    load(&f, d, 1);
    event_vm_run(&f.vm, 10);
    CHECK(f.state.raw[ST_GOLD] == -1);

    make(&f);
    EventCommand e[] = { { EVOP_SET_STATE, ST_ATTACK, 0x8000 } };
    load(&f, e, 1);
    event_vm_run(&f.vm, 10);
    CHECK(f.state.raw[ST_ATTACK] == -32768);
}

static void test_halve_life_and_gameover(void)
{
    Fixture f; make(&f);
    f.state.raw[ST_LIFE] = 101;
    EventCommand c[] = { { EVOP_HALVE_LIFE, 0, 0 } };
    load(&f, c, 1);
    event_vm_run(&f.vm, 10);
    CHECK(f.state.raw[ST_LIFE] == 50);
    CHECK(!f.vm.gameover);

    make(&f);
    f.state.raw[ST_LIFE] = 0;
    EventCommand d[] = { { EVOP_SET_STATE, ST_GOLD, 9 }, { EVOP_HALVE_LIFE, 0, 0 } };
    load(&f, d, 2);
    event_vm_run(&f.vm, 10);
    CHECK(f.vm.gameover);
    CHECK(f.state.raw[ST_GOLD] == 0);
    CHECK(f.vm.count == 0);
}

static void test_add_life_zero_truncates(void)
{
    Fixture f; make(&f);
    f.state.raw[ST_LIFE] = 10;
    EventCommand c[] = { { EVOP_SET_STATE, ST_GOLD, 99 }, { EVOP_ADD_LIFE, (uint16_t)-10, 0 } };
    load(&f, c, 2);
    event_vm_run(&f.vm, 10);
    CHECK(f.vm.gameover);
    CHECK(f.state.raw[ST_GOLD] == 0);
}

static void sub_one(EventVm *vm, int kind, int sub, int arg2, void *user)
{
    (void)kind; (void)sub; (void)arg2; (void)user;
    event_vm_append(vm, EVOP_SET_STATE, ST_GOLD, 77);
}

static void sub_two(EventVm *vm, int kind, int sub, int arg2, void *user)
{
    (void)kind; (void)sub; (void)arg2; (void)user;
    event_vm_append(vm, EVOP_SET_STATE, ST_GOLD, 10);
    event_vm_append(vm, EVOP_SET_STATE, ST_GOLD, 20);
}

static void test_splice(void)
{
    Fixture f; make(&f);
    f.vm.sub_fn = sub_one;
    EventCommand c[] = { { EVOP_SET_STATE, ST_GOLD, 5 }, { EVOP_SUBDISPATCH, 1, 0 } };
    load(&f, c, 2);
    event_vm_run(&f.vm, 10);
    CHECK(f.state.raw[ST_GOLD] == 5);
    CHECK(count_kind(&f.log, EVLOG_SUBDISPATCH) == 1);
    int vals[4], n = 0;
    for (int i = 0; i < f.log.n; i++)
        if (f.log.e[i].kind == EVLOG_SET_STATE && n < 4)
            vals[n++] = f.log.e[i].b;
    CHECK(n == 2 && vals[0] == 77 && vals[1] == 5);

    make(&f);
    f.vm.sub_fn = sub_two;
    EventCommand d[] = { { EVOP_NOP, 0, 0 }, { EVOP_SUBDISPATCH, 1, 0 } };
    load(&f, d, 2);
    event_vm_run(&f.vm, 10);
    n = 0;
    for (int i = 0; i < f.log.n; i++)
        if (f.log.e[i].kind == EVLOG_SET_STATE && n < 4)
            vals[n++] = f.log.e[i].b;
    CHECK(n == 2 && vals[0] == 20 && vals[1] == 10);
}

static void test_floor_change(void)
{
    Fixture f; make(&f);
    f.state.raw[ST_FLOOR] = 3;
    EventCommand c[] = { { EVOP_SUBDISPATCH, 0x0b, 20 } };
    load(&f, c, 1);
    event_vm_run(&f.vm, 10);
    CHECK(f.state.raw[ST_FLOOR] == 20);
    const EventVmLog *e = first_kind(&f.log, EVLOG_CHANGE_FLOOR);
    CHECK(e && e->a == 20);
}

static void test_actor_chain(void)
{
    Fixture f; make(&f);
    EventCommand c[] = { { EVOP_DRAW_ACTOR, 1, 0x10 }, { EVOP_DRAW_ACTOR, 1, 0x11 } };
    load(&f, c, 2);
    CHECK(event_vm_run(&f.vm, 10) == 1);
    CHECK(count_kind(&f.log, EVLOG_DRAW_ACTOR) == 2);
    CHECK(f.log.e[1].c == 1);

    make(&f);
    EventCommand d[] = { { EVOP_DRAW_ACTOR, 1, 0x10 }, { EVOP_DRAW_ACTOR, 0, 0x11 } };
    load(&f, d, 2);
    CHECK(event_vm_run(&f.vm, 10) == 2);
    for (int i = 0; i < f.log.n; i++)
        CHECK(f.log.e[i].c == 0);
    CHECK(f.vm.count == 0);
}

static void test_three_sprite_previous(void)
{
    Fixture f; make(&f);
    EventCommand c[] = { { EVOP_NOP, 7, 8 }, { EVOP_THREE_SPRITE_ANIM, 1, 2 } };
    load(&f, c, 2);
    event_vm_run(&f.vm, 10);
    const EventVmLog *e = first_kind(&f.log, EVLOG_THREE_SPRITE_ANIM);
    CHECK(e && e->a == 1 && e->b == 2 && e->c == 7 && e->d == 8);
}

static void test_all_opcodes_known(void)
{
    for (int op = 0; op < EVOP_COUNT; op++) {
        Fixture f; make(&f);
        EventCommand c[] = { { (uint16_t)op, 0, 0 } };
        load(&f, c, 1);
        event_vm_run(&f.vm, 10);
        CHECK(count_kind(&f.log, EVLOG_UNKNOWN_OPCODE) == 0);
    }
    Fixture f; make(&f);
    EventCommand c[] = { { EVOP_COUNT, 0, 0 } };
    load(&f, c, 1);
    event_vm_run(&f.vm, 10);
    CHECK(count_kind(&f.log, EVLOG_UNKNOWN_OPCODE) == 1);
}

static void test_presentation_logged(void)
{
    struct { int op; EventVmLogKind kind; } cases[] = {
        { EVOP_SOUND, EVLOG_SOUND }, { EVOP_SET_SPRITE, EVLOG_SPRITE },
        { EVOP_ALTAR, EVLOG_ALTAR }, { EVOP_SHOW_ITEMS, EVLOG_SHOW_ITEMS },
        { EVOP_START_SUBEVENT, EVLOG_START_SUBEVENT },
        { EVOP_MONSTER_MOVE, EVLOG_MONSTER_MOVE },
        { EVOP_DOOR_OPEN, EVLOG_DOOR_OPEN }, { EVOP_STOP_BGM, EVLOG_STOP_BGM },
        { EVOP_RESET, EVLOG_RESET },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Fixture f; make(&f);
        EventCommand c[] = { { (uint16_t)cases[i].op, 1, 2 } };
        load(&f, c, 1);
        event_vm_run(&f.vm, 10);
        CHECK(count_kind(&f.log, cases[i].kind) == 1);
    }
}

static void test_reset(void)
{
    Fixture f; make(&f);
    f.state.raw[ST_OPTION_A] = 7;
    f.state.raw[ST_OPTION_B] = 8;
    EventCommand c[] = { { EVOP_RESET, 0, 0 } };
    load(&f, c, 1);
    event_vm_run(&f.vm, 10);
    CHECK(f.state.raw[ST_OPTION_A] == 0 && f.state.raw[ST_OPTION_B] == 0);
    CHECK(f.vm.gameover);
}

static void test_subdispatch_variants(void)
{
    struct { int sub; EventVmLogKind kind; } cases[] = {
        { 1, EVLOG_SUBDISPATCH }, { 2, EVLOG_SUBDISPATCH }, { 3, EVLOG_SUBDISPATCH },
        { 4, EVLOG_VAMPIRE }, { 5, EVLOG_SUBDISPATCH }, { 6, EVLOG_GAMEOVER },
        { 7, EVLOG_TITLE }, { 8, EVLOG_PLAYER_UPDATE }, { 9, EVLOG_PLAYER_ANIMATION },
        { 10, EVLOG_SUBDISPATCH }, { 0xb, EVLOG_CHANGE_FLOOR }, { 0xc, EVLOG_OPENING9 },
        { 0xd, EVLOG_HIDE_MESSAGE }, { 0xe, EVLOG_HIDE_IMAGES }, { 0xf, EVLOG_FADE },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Fixture f; make(&f);
        f.state.raw[ST_FLOOR] = 5;
        EventCommand c[] = { { EVOP_SUBDISPATCH, (uint16_t)cases[i].sub, 6 } };
        load(&f, c, 1);
        event_vm_run(&f.vm, 10);
        CHECK(count_kind(&f.log, cases[i].kind) >= 1);
    }
    /* Subcommand 2 writes map index 0x1b = 0x5b on the current floor. */
    Fixture f; make(&f);
    f.state.raw[ST_FLOOR] = 3;
    EventCommand c[] = { { EVOP_SUBDISPATCH, 2, 0 } };
    load(&f, c, 1);
    event_vm_run(&f.vm, 10);
    CHECK(map_tile(&f.map, 3, 0x1b) == 0x5b);
    /* Vampire stage is scaled by 5/4 when option A is 1. */
    make(&f);
    f.state.raw[ST_OPTION_A] = 1;
    EventCommand v[] = { { EVOP_SUBDISPATCH, 4, 8 } };
    load(&f, v, 1);
    event_vm_run(&f.vm, 10);
    const EventVmLog *e = first_kind(&f.log, EVLOG_VAMPIRE);
    CHECK(e && e->a == 10);
}

static void test_tick(void)
{
    Fixture f; make(&f);
    EventCommand c[] = { { EVOP_NOP, 0, 0 }, { EVOP_NOP, 0, 0 }, { EVOP_NOP, 0, 0 } };
    load(&f, c, 3);
    CHECK(event_vm_tick(&f.vm) == 2);
    CHECK(f.vm.count == 1);
    CHECK(event_vm_tick(&f.vm) == 1);
    CHECK(f.vm.count == 0);
    CHECK(event_vm_tick(&f.vm) == 0);
}

static void test_append_and_overflow(void)
{
    Fixture f; make(&f);
    for (int i = 0; i < EVENT_VM_CAPACITY - 1; i++)
        CHECK(event_vm_append(&f.vm, EVOP_NOP, 0, 0));
    CHECK(!f.vm.overflow);
    CHECK(!event_vm_append(&f.vm, EVOP_NOP, 0, 0));
    CHECK(f.vm.overflow);
    event_vm_clear(&f.vm);
    CHECK(!event_vm_active(&f.vm));
}

int main(void)
{
    test_tail_execution_order();
    test_nops();
    test_set_state_direct();
    test_set_tile();
    test_life_delta();
    test_monster_delta();
    test_battle_hit_ignores_cross();
    test_lucky_gold_and_signed();
    test_halve_life_and_gameover();
    test_add_life_zero_truncates();
    test_splice();
    test_floor_change();
    test_actor_chain();
    test_three_sprite_previous();
    test_all_opcodes_known();
    test_presentation_logged();
    test_reset();
    test_subdispatch_variants();
    test_tick();
    test_append_and_overflow();
    if (failures) {
        fprintf(stderr, "test_event_vm: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_event_vm: ok\n");
    return 0;
}
