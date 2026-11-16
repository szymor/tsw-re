#include "event_vm.h"

#include <string.h>

/* See event_vm.h and docs/re/12-event-vm.md.  Addresses refer to the pinned
 * TSW.exe (sha256 41cb9844...2779). */

static void emit_log(EventVm *vm, EventVmLogKind kind,
                     int32_t a, int32_t b, int32_t c, int32_t d)
{
    if (!vm->observer)
        return;
    EventVmLog e = { kind, a, b, c, d };
    vm->observer(vm, &e, vm->observer_user);
}

int event_vm_speed_interval_ms(int speed)
{
    switch (speed) {
    case EVM_SPEED_HIGH: return 150;   /* speedhigh 0x47f47c */
    case EVM_SPEED_LOW: return 350;    /* speedlow 0x47f544 */
    default: return 250;               /* speedmiddle 0x47f4e0 */
    }
}

int event_vm_floor_bgm(int floor, int current)
{
    if (floor == 0) return 0x15;
    if (floor >= 1 && floor <= 10) return 5;
    if (floor >= 11 && floor <= 20) return 6;
    if (floor >= 21 && floor <= 30) return 0x11;
    if (floor >= 31 && floor <= 40) return 8;
    if (floor >= 41 && floor <= 49) return 9;
    if (floor == 50) return 10;
    return current;                     /* no case: unchanged */
}

int event_vm_door_interval_ms(int speed)
{
    switch (speed) {
    case EVM_SPEED_HIGH: return 8;
    case EVM_SPEED_LOW: return 12;
    default: return 10;
    }
}

int event_vm_opening9_interval_ms(int speed, int phase, int frame)
{
    int i = speed == EVM_SPEED_HIGH ? 0 : speed == EVM_SPEED_LOW ? 2 : 1;
    switch (phase) {
    case 0:
    case 2:
        /* end of the phase-0/2 frames: back to the speed setting */
        if ((phase == 0 && frame == 0xf7) || (phase == 2 && frame == 0xf8))
            return (const int[]){ 150, 250, 350 }[i];
        return 0;
    case 1:
        if (frame == 0)
            return (const int[]){ 30, 50, 70 }[i];     /* open2 reveal */
        if (frame == 400)                              /* 32-px tiles */
            return (const int[]){ 130, 200, 300 }[i];  /* UI restored */
        return 0;
    case 4: return 100;
    case 5: return 60;
    case 6: return 450;
    default: return 0;                                 /* 3, 7, 8, ... */
    }
}

int event_vm_stairs_interval_ms(int speed)
{
    switch (speed) {
    case EVM_SPEED_HIGH: return 2;
    case EVM_SPEED_LOW: return 10;
    default: return 6;
    }
}

int event_vm_door_tile(int kind)
{
    /* dooropen (0x4770e0) switch on the current command's arg2, table
     * 0x477146: kinds 1..12 write map[floor][arg1]; 5 -> 4, 8 -> 8,
     * all others -> 6 (floor).  Kind 0 / >12 fall through untouched. */
    if (kind < 1 || kind > 12)
        return -1;
    if (kind == 5)
        return 4;
    if (kind == 8)
        return 8;
    return 6;
}

void event_vm_syokidata2_zeno(EventVm *vm)
{
    if (!vm->state)
        return;
    int32_t flag = vm->state->raw[0x9c];
    if (flag == 0) {
        vm->zeno[0] = 8000; vm->zeno[1] = 5000; vm->zeno[2] = 1000;
    } else if (flag == 1) {
        int32_t k = vm->state->raw[ST_OPTION_A] + 1;
        vm->zeno[0] = k * 800; vm->zeno[1] = k * 500; vm->zeno[2] = k * 100;
    }
}

void event_vm_init(EventVm *vm, StateBlock *state, MapBlock *map)
{
    memset(vm, 0, sizeof(*vm));
    /* The table as shipped (TSW.exe 0x489a00). */
    vm->zeno[0] = 8000; vm->zeno[1] = 5000; vm->zeno[2] = 1000;
    vm->state = state;
    vm->map = map;
    vm->speed = EVM_SPEED_MIDDLE;
    vm->interval_ms = event_vm_speed_interval_ms(vm->speed);
}

void event_vm_clear(EventVm *vm)
{
    vm->count = 0;
    memset(vm->commands, 0, sizeof(vm->commands));
    vm->gameover = false;
    vm->overflow = false;
}

bool event_vm_load(EventVm *vm, const EventCommand *cmds, int n)
{
    if (n < 0 || n >= EVENT_VM_CAPACITY)
        return false;
    memset(vm->commands, 0, sizeof(vm->commands));
    for (int i = 0; i < n; i++)
        vm->commands[i + 1] = cmds[i];
    vm->count = n;
    return true;
}

bool event_vm_append(EventVm *vm, int opcode, int arg1, int arg2)
{
    if (vm->count + 1 >= EVENT_VM_CAPACITY) {
        vm->overflow = true;
        emit_log(vm, EVLOG_OVERFLOW, opcode, arg1, arg2, 0);
        return false;
    }
    vm->count++;
    vm->commands[vm->count].opcode = (uint16_t)opcode;
    vm->commands[vm->count].arg1 = (uint16_t)arg1;
    vm->commands[vm->count].arg2 = (uint16_t)arg2;
    return true;
}

bool event_vm_active(const EventVm *vm)
{
    return vm->count > 0;
}

static int32_t st(const EventVm *vm, int i)
{
    return vm->state ? vm->state->raw[i] : 0;
}

static void st_set(EventVm *vm, int i, int32_t v)
{
    if (vm->state)
        vm->state->raw[i] = v;
}

static int tile_offset(int floor, int index)
{
    int off = map_floor_offset(floor);
    if (off < 0)
        return -1;
    off += index;
    if (off < 0 || off >= MAP_BLOCK_SIZE)
        return -1;
    return off;
}

uint8_t event_vm_tile(const EventVm *vm, int floor, int index)
{
    int off = tile_offset(floor, index);
    if (!vm->map || off < 0)
        return 0;
    return vm->map->bytes[off];
}

void event_vm_set_tile(EventVm *vm, int floor, int index, int value)
{
    int off = tile_offset(floor, index);
    if (!vm->map || off < 0) {
        emit_log(vm, EVLOG_TILE_OOB, floor, index, value, 0);
        return;
    }
    vm->map->bytes[off] = (uint8_t)value;
}

static const EventCommand *previous(const EventVm *vm)
{
    if (vm->count <= 1)
        return NULL;
    return &vm->commands[vm->count - 1];
}

static void clamp_life(EventVm *vm)
{
    if (st(vm, ST_LIFE) < 0)
        st_set(vm, ST_LIFE, 0);
}

void event_vm_death_timeline(EventVm *vm)
{
    if (st(vm, EVM_ST_BGM) != 0)
        st_set(vm, EVM_ST_BGM, 0x0e);
    /* Appended in this order, so run from the end (LIFO). */
    event_vm_append(vm, EVOP_NOP, 0, 0);
    event_vm_append(vm, EVOP_RESET, 0, 0);
    for (int i = 0; i < 5; i++)
        event_vm_append(vm, EVOP_NOP, 0, 0);
    event_vm_append(vm, EVOP_START_SUBEVENT, 0x5d, 0);
    for (int i = 0; i < 4; i++)
        event_vm_append(vm, EVOP_NOP, 0, 0);
    if (st(vm, EVM_ST_BGM) != 0) {
        for (int i = 0; i < 9; i++)
            event_vm_append(vm, EVOP_NOP, 0, 0);
        event_vm_append(vm, EVOP_SOUND, 5, 0);
    }
}

/* The player's death window (op 0x03, and ops 0x10/0x13 reaching 0 life):
 * BGM 0x0e, state[0x0c] = 1, g_subevent_id 9, g_dialog_selector 0x1e and
 * zyouout, which appends the game-over timeline. */
static void death_window(EventVm *vm, int from_op3)
{
    if (st(vm, EVM_ST_BGM) != 0)
        st_set(vm, EVM_ST_BGM, 0x0e);
    st_set(vm, 0x0c, 1);                /* battle_render_flag */
    event_vm_death_timeline(vm);
    emit_log(vm, EVLOG_DEATH_WINDOW, from_op3, 0, 0, 0);
}

static void gameover_from_life(EventVm *vm)
{
    vm->gameover = true;
    emit_log(vm, EVLOG_GAMEOVER, 0, 0, 0, 0);
    /* The original sets the cursor to 1 (dropping the rest of the
     * timeline) before the death window appends on top of it. */
    vm->count = 1;
    death_window(vm, 0);
}

static int attack_multiplier(const EventVm *vm)
{
    int i = vm->monster_index;
    if ((i == 12 || i == 13 || i == 17)
        && st(vm, ST_SPECIAL_FIRST + EVM_SLOT_CROSS) == 1)
        return 2;
    if (i == 19 && st(vm, ST_SPECIAL_FIRST + EVM_SLOT_DRAGON_SLAYER) == 1)
        return 2;
    return 1;
}

static void monster_delta(EventVm *vm, bool allow_multiplier)
{
    int32_t diff = st(vm, ST_OPTION_A);
    int32_t mult = allow_multiplier ? attack_multiplier(vm) : 1;
    int32_t delta = vm->monster.defense * (diff + 1) - st(vm, ST_ATTACK) * mult;
    if (delta > 0)
        delta = 0;
    vm->monster_hp += delta;
    if (vm->monster_hp < 0)
        vm->monster_hp = 0;
    emit_log(vm, EVLOG_MONSTER_HP, vm->monster_hp, delta, 0, 0);
}

/* timerin (0x47f5a8): Timer2 back to the speed-menu interval (speedhigh /
 * speedmiddle / speedlow), undoing dooropen's 8/10/12 ms. */
static void timerin(EventVm *vm)
{
    vm->interval_ms = event_vm_speed_interval_ms(vm->speed);
    emit_log(vm, EVLOG_TIMERIN, 0, 0, 0, 0);
}

static void op_subdispatch(EventVm *vm, const EventCommand *cmd)
{
    int sub = cmd->arg1;
    switch (sub) {
    case 1: case 2: case 3: case 5: case 10:
        /* Splice (0x4531da): the sub-handler appends over this command. */
        vm->count--;
        if (sub == 2)
            event_vm_set_tile(vm, st(vm, ST_FLOOR), 0x1b, 0x5b);
        emit_log(vm, EVLOG_SUBDISPATCH, sub, cmd->arg2, 0, 0);
        if (vm->sub_fn)
            vm->sub_fn(vm, EVOP_SUBDISPATCH, sub, cmd->arg2, vm->sub_user);
        vm->count++;
        break;
    case 4: {
        /* banpaiawork (0x47c978): presentation only.  The stage is arg2,
         * scaled by 5/4 only in the 800x500 layout (option.dat byte 8,
         * 0x489ba4), which the remake does not use. */
        emit_log(vm, EVLOG_VAMPIRE, cmd->arg2, 0, 0, 0);
        break;
    }
    case 6:
        vm->count--;
        if (st(vm, ST_OPTION_A) == 0)
            st_set(vm, ST_OPTION_A, 0x2b);
        else
            st_set(vm, ST_OPTION_B, 1);
        vm->gameover = true;
        emit_log(vm, EVLOG_GAMEOVER, 1, 0, 0, 0);
        if (vm->sub_fn)
            vm->sub_fn(vm, EVOP_SUBDISPATCH, sub, cmd->arg2, vm->sub_user);
        vm->count++;
        break;
    case 7:
        emit_log(vm, EVLOG_TITLE, st(vm, ST_OPTION_A) ? 1 : 0, 0, 0, 0);
        break;
    case 8:
        emit_log(vm, EVLOG_PLAYER_UPDATE, 0, 0, 0, 0);
        break;
    case 9:
        emit_log(vm, EVLOG_PLAYER_ANIMATION, 0, 0, 0, 0);
        break;
    case 0xb: {
        /* 0x45362b: g_battle_turns = 0 (the engine's aux), floor = arg2,
         * mhyouji (clears state[0xa6]), disp.  On floor 50: state[0xc] = 0,
         * the player at the floor's up-arrival byte, facing 4. */
        int floor = event_vm_s16(cmd->arg2);
        st_set(vm, ST_FLOOR, floor);
        st_set(vm, 0xa6, 0);
        if (floor == 50) {
            st_set(vm, 0x0c, 0);
            int b = vm->map->bytes[50 * 123];
            st_set(vm, ST_X, b % 11);
            st_set(vm, ST_Y, b / 11);
            st_set(vm, ST_FACING, 4);
        }
        emit_log(vm, EVLOG_CHANGE_FLOOR, floor, 0, 0, 0);
        break;
    }
    case 0xc: {
        /* openning9 (0x482e60): the phase is the PREVIOUS command's arg1
         * (0x48c748 + count*6), the frame is this command's arg2.  It owns
         * the opening/ending pacing (TTimer.SetInterval 0x42c464 on Timer2)
         * and phase 8 stops Timer2; drawing is presentation. */
        const EventCommand *p = previous(vm);
        int phase = p ? event_vm_s16(p->arg1) : 0;
        int frame = event_vm_s16(cmd->arg2);
        int iv = event_vm_opening9_interval_ms(vm->speed, phase, frame);
        if (iv > 0)
            vm->interval_ms = iv;
        emit_log(vm, EVLOG_OPENING9, phase, frame, iv, 0);
        break;
    }
    case 0xd:
        emit_log(vm, EVLOG_HIDE_MESSAGE, 0, 0, 0, 0);
        break;
    case 0xe:
        emit_log(vm, EVLOG_HIDE_IMAGES, 0, 0, 0, 0);
        break;
    case 0xf:
        emit_log(vm, EVLOG_FADE, 0, 0, 0, 0);
        break;
    default:
        emit_log(vm, EVLOG_SUBDISPATCH_UNKNOWN, sub, cmd->arg2, 0, 0);
        break;
    }
}

bool event_vm_step(EventVm *vm)
{
    if (vm->count <= 0 || vm->count >= EVENT_VM_CAPACITY)
        return false;
    /* Copy: splices may overwrite the slot while we still need its args. */
    EventCommand cur = vm->commands[vm->count];
    const EventCommand *cmd = &cur;
    const EventCommand *prev;

    switch (cmd->opcode) {
    case EVOP_NOP:
    case EVOP_NOP_12:
        emit_log(vm, EVLOG_NOP, cmd->arg1, cmd->arg2, 0, 0);
        break;
    case EVOP_SOUND:
        emit_log(vm, EVLOG_SOUND, cmd->arg1, cmd->arg2, 0, 0);
        break;
    case EVOP_SET_SPRITE:
        if (cmd->arg1)
            vm->current_sprite = cmd->arg1;
        emit_log(vm, EVLOG_SPRITE, cmd->arg1, 0, 0, 0);
        break;
    case EVOP_ALTAR:
        /* stackwork op 0x03: redraw the player's cell, Timer2 off, death
         * window. */
        emit_log(vm, EVLOG_ALTAR, cmd->arg1, cmd->arg2, 0, 0);
        death_window(vm, 1);
        break;
    case EVOP_LIFE_DELTA: {
        int32_t diff = st(vm, ST_OPTION_A);
        int32_t delta = st(vm, ST_DEFENSE) - vm->monster.attack * (diff + 1);
        if (delta > 0)
            delta = 0;
        st_set(vm, ST_LIFE, st(vm, ST_LIFE) + delta);
        clamp_life(vm);
        emit_log(vm, EVLOG_LIFE, st(vm, ST_LIFE), delta, 0, 0);
        break;
    }
    case EVOP_MONSTER_DELTA:
        monster_delta(vm, true);
        break;
    case EVOP_SET_TILE:
    case EVOP_SET_TILE_SILENT: {
        int floor = st(vm, ST_FLOOR);
        int index = event_vm_s16(cmd->arg1);
        int silent = cmd->opcode == EVOP_SET_TILE_SILENT;
        event_vm_set_tile(vm, floor, index, cmd->arg2);
        emit_log(vm, EVLOG_SET_TILE, floor, index, cmd->arg2, silent);
        break;
    }
    case EVOP_ADD_GOLD: {
        int32_t amount = event_vm_s16(cmd->arg1);
        int32_t lucky = st(vm, ST_SPECIAL_FIRST + EVM_SLOT_LUCKY_GOLD);
        st_set(vm, ST_GOLD, st(vm, ST_GOLD) + amount * (lucky + 1));
        emit_log(vm, EVLOG_GOLD, st(vm, ST_GOLD), 0, 0, 0);
        break;
    }
    case EVOP_SHOW_ITEMS:
        emit_log(vm, EVLOG_SHOW_ITEMS, 0, 0, 0, 0);
        break;
    case EVOP_THREE_SPRITE_ANIM:
        prev = previous(vm);
        emit_log(vm, EVLOG_THREE_SPRITE_ANIM, cmd->arg1, cmd->arg2,
                 prev ? prev->arg1 : 0, prev ? prev->arg2 : 0);
        break;
    case EVOP_START_SUBEVENT:
        emit_log(vm, EVLOG_START_SUBEVENT, cmd->arg1, cmd->arg1 + 10,
                 cmd->arg2, 0);
        if (vm->sub_fn)
            vm->sub_fn(vm, EVOP_START_SUBEVENT, cmd->arg1, cmd->arg2,
                       vm->sub_user);
        break;
    case EVOP_SUBDISPATCH:
        op_subdispatch(vm, cmd);
        break;
    case EVOP_MONSTER_MOVE: {
        /* monidou (0x480384): move the tile at arg1 to arg2 and leave
         * floor behind: map[arg2] = map[arg1] (0x480a70), map[arg1] = 6
         * (0x480a83).  The animation is presentation. */
        int floor = st(vm, ST_FLOOR);
        int from = event_vm_s16(cmd->arg1);
        int to = event_vm_s16(cmd->arg2);
        uint8_t raw = event_vm_tile(vm, floor, from);
        event_vm_set_tile(vm, floor, to, raw);
        event_vm_set_tile(vm, floor, from, 0x06);
        emit_log(vm, EVLOG_MONSTER_MOVE, from, to, raw, 0);
        break;
    }
    case EVOP_DOOR_OPEN: {
        int index = event_vm_s16(cmd->arg1);
        int kind = cmd->arg2;
        int tile = event_vm_door_tile(kind);
        if (tile >= 0) {
            vm->interval_ms = event_vm_door_interval_ms(vm->speed);
            prev = previous(vm);
            vm->anim_progress = prev ? event_vm_s16(prev->arg2) : 0;
            event_vm_set_tile(vm, st(vm, ST_FLOOR), index, tile);
            if (vm->anim_progress == 0x10)
                timerin(vm);
        }
        emit_log(vm, EVLOG_DOOR_OPEN, index, kind, tile, vm->anim_progress);
        break;
    }
    case EVOP_BATTLE_HIT:
        prev = previous(vm);
        emit_log(vm, EVLOG_BATTLE_HIT, cmd->arg1, cmd->arg2,
                 prev ? prev->arg1 : 0, prev ? prev->arg2 : 0);
        monster_delta(vm, false);
        break;
    case EVOP_STOP_BGM:
        /* 0x454038: stop the media player, soundcheck (0x47c8f8) picks the
         * floor's BGM into state[0x5a], soundplay (0x47c2bc) plays it. */
        st_set(vm, EVM_ST_BGM, event_vm_floor_bgm(st(vm, ST_FLOOR),
                                                 st(vm, EVM_ST_BGM)));
        emit_log(vm, EVLOG_STOP_BGM, st(vm, EVM_ST_BGM), 0, 0, 0);
        break;
    case EVOP_ADD_LIFE: {
        int32_t delta = event_vm_s16(cmd->arg1);
        st_set(vm, ST_LIFE, st(vm, ST_LIFE) + delta);
        clamp_life(vm);
        emit_log(vm, EVLOG_LIFE, st(vm, ST_LIFE), delta, 0, 0);
        if (st(vm, ST_LIFE) == 0)
            gameover_from_life(vm);
        break;
    }
    case EVOP_HALVE_LIFE:
        st_set(vm, ST_LIFE, st(vm, ST_LIFE) >> 1);
        clamp_life(vm);
        emit_log(vm, EVLOG_LIFE, st(vm, ST_LIFE), 0, 1, 0);
        if (st(vm, ST_LIFE) == 0)
            gameover_from_life(vm);
        break;
    case EVOP_SET_STATE: {
        int index = event_vm_s16(cmd->arg1);
        int value = event_vm_s16(cmd->arg2);
        if (index >= 0 && index < STATE_INT_COUNT) {
            st_set(vm, index, value);
            emit_log(vm, EVLOG_SET_STATE, index, value, 0, 0);
        } else {
            emit_log(vm, EVLOG_SET_STATE_OOB, index, value, 0, 0);
        }
        break;
    }
    case EVOP_DRAW_ACTOR: {
        int mode = event_vm_s16(cmd->arg1);
        int index = event_vm_s16(cmd->arg2);
        int last = index; /* g_0x48c570: the square handled last */
        emit_log(vm, EVLOG_DRAW_ACTOR, mode, index, 0, 0);
        prev = previous(vm);
        if (prev && prev->opcode == EVOP_DRAW_ACTOR
            && event_vm_s16(prev->arg1) == mode) {
            /* The preceding same-mode 0x15 is consumed in this step. */
            int pmode = event_vm_s16(prev->arg1);
            int pindex = event_vm_s16(prev->arg2);
            vm->count--;
            last = pindex;
            emit_log(vm, EVLOG_DRAW_ACTOR, pmode, pindex, 1, 0);
        }
        /* 0x4547b4: only the reveal (mode 1) reaches the timerin check, after
         * the pair; mode 0 jumps straight to the common tail, so covering
         * square 0x79 leaves the stairs' fast Timer2 running. */
        if (mode == 1 && last == 0x79) {
            timerin(vm);
            emit_log(vm, EVLOG_TIMER1, 1, 0, 0, 0);
        }
        break;
    }
    case EVOP_RESET:
        /* 0x4547d3: clear both option words, then gameover (0x454da0):
         * syokidata resets the state and rewrites the initial map (the
         * observer does that: it owns the map data), syokidata2 empties
         * the timeline (g_tl_count = 0), so the common tail leaves -1. */
        st_set(vm, ST_OPTION_A, 0);
        st_set(vm, ST_OPTION_B, 0);
        vm->count = 0;
        vm->gameover = true;
        emit_log(vm, EVLOG_RESET, 0, 0, 0, 0);
        break;
    default:
        emit_log(vm, EVLOG_UNKNOWN_OPCODE, cmd->opcode, cmd->arg1,
                 cmd->arg2, 0);
        break;
    }
    vm->count--; /* common tail 0x4547e8 */
    return true;
}

int event_vm_run(EventVm *vm, int max_steps)
{
    int steps = 0;
    while (vm->count > 0 && steps < max_steps) {
        if (!event_vm_step(vm))
            break;
        steps++;
    }
    return steps;
}

int event_vm_tick(EventVm *vm)
{
    int steps = 0;
    for (int i = 0; i < EVENT_VM_STEPS_PER_TICK; i++) {
        if (vm->count < 1)
            break;
        event_vm_step(vm);
        steps++;
    }
    return steps;
}
