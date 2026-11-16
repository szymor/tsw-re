#ifndef TSW_EVENT_VM_H
#define TSW_EVENT_VM_H

/* C port of the original 6-byte event timeline VM (`stackwork`, 0x4526c0).
 *
 * Reference model: docs/re/tools/event_vm.py; semantics: docs/re/12-event-vm.md.
 *
 * The original stores a command count at 0x48c5ac and u16 (opcode, arg1,
 * arg2) triples at 0x48c74c.  Index 0 is a sentinel.  Handlers append by
 * incrementing the count and writing the new slot; `stackwork` executes the
 * command at the current count and then decrements it, so the timeline is a
 * tail/LIFO sequence.  `0x0b` subcommands 1/2/3/5/6/10 splice: decrement,
 * call the sub-handler (which appends over the current slot), increment.
 *
 * The VM owns no presentation.  Presentation opcodes are reported through
 * the observer callback as typed log entries; state/map effects are applied
 * directly to the caller's StateBlock/MapBlock.  The VM is dependency-free
 * (no SDL, no Lua) so it can be unit-tested and driven by Lua builders.
 */

#include <stdbool.h>
#include <stdint.h>

#include "map.h"
#include "state.h"

/* Capacity of the command buffer, including the index-0 sentinel.  The
 * longest recovered stream (new-game `openning2`) is 651 commands. */
#define EVENT_VM_CAPACITY 4096

/* Commands consumed per `timer2timer` invocation (0x45265c). */
#define EVENT_VM_STEPS_PER_TICK 2

/* Timer2 (`timer2timer`) interval, recovered statically (12-event-vm.md §10).
 * Form default 300 ms; `speedhigh`/`speedmiddle`/`speedlow` (option.dat
 * byte 3 = 1/2/3, default 2) set 150/250/350 ms.  `dooropen` (op 0x0d)
 * sets 8/10/12 ms and `kaidanwork` (stairs) 2/6/10 ms by the same speed.
 * The last setter wins; `timerin` (0x47f5a8, the last door frame and op 0x15
 * mode 1 whose last-handled cell is 0x79) restores the speed value. */
#define EVENT_VM_FORM_INTERVAL_MS 300
enum { EVM_SPEED_HIGH = 1, EVM_SPEED_MIDDLE = 2, EVM_SPEED_LOW = 3 };
int event_vm_speed_interval_ms(int speed);
int event_vm_door_interval_ms(int speed);
/* soundcheck (0x47c8f8): the floor's BGM id for state[0x5a]; floors with no
 * case keep `current`. */
int event_vm_floor_bgm(int floor, int current);
/* state[0x5a]: the BGM/sound state op 0x0f rewrites through soundcheck. */
#define EVM_ST_BGM 0x5a
int event_vm_stairs_interval_ms(int speed);
/* Interval set by openning9 (op 0x0b/0xc) for (phase, frame), or 0 when it
 * leaves Timer2 alone.  Phase 8 disables Timer2 instead (engine). */
int event_vm_opening9_interval_ms(int speed, int phase, int frame);

enum {
    EVOP_NOP = 0x00,
    EVOP_SOUND = 0x01,
    EVOP_SET_SPRITE = 0x02,
    EVOP_ALTAR = 0x03,
    EVOP_LIFE_DELTA = 0x04,
    EVOP_MONSTER_DELTA = 0x05,
    EVOP_SET_TILE = 0x06,
    EVOP_ADD_GOLD = 0x07,
    EVOP_SHOW_ITEMS = 0x08,
    EVOP_THREE_SPRITE_ANIM = 0x09,
    EVOP_START_SUBEVENT = 0x0a,
    EVOP_SUBDISPATCH = 0x0b,
    EVOP_MONSTER_MOVE = 0x0c,
    EVOP_DOOR_OPEN = 0x0d,
    EVOP_BATTLE_HIT = 0x0e,
    EVOP_STOP_BGM = 0x0f,
    EVOP_ADD_LIFE = 0x10,
    EVOP_SET_TILE_SILENT = 0x11,
    EVOP_NOP_12 = 0x12,
    EVOP_HALVE_LIFE = 0x13,
    EVOP_SET_STATE = 0x14,
    EVOP_DRAW_ACTOR = 0x15,
    EVOP_RESET = 0x16,
    EVOP_COUNT = 0x17
};

/* Special-item slots used by the VM's battle formulas. */
enum {
    EVM_SLOT_CROSS = 3,
    EVM_SLOT_DRAGON_SLAYER = 10,
    EVM_SLOT_LUCKY_GOLD = 14
};

typedef enum {
    EVLOG_NOP = 0,
    EVLOG_SOUND,             /* a=index b=arg2 */
    EVLOG_SPRITE,            /* a=sprite id (0 = keep) */
    EVLOG_ALTAR,             /* a=arg1 b=arg2 */
    EVLOG_LIFE,              /* a=new life b=delta (c=1: halve) */
    EVLOG_MONSTER_HP,        /* a=new hp b=delta */
    EVLOG_SET_TILE,          /* a=floor b=index c=value d=silent */
    EVLOG_GOLD,              /* a=new gold */
    EVLOG_SHOW_ITEMS,
    EVLOG_THREE_SPRITE_ANIM, /* a,b=current args c,d=previous args */
    EVLOG_START_SUBEVENT,    /* a=arg1 b=selector (arg1+10) c=arg2 */
    EVLOG_SUBDISPATCH,       /* a=sub b=arg2 (splice subcommands) */
    EVLOG_VAMPIRE,           /* a=stage (scaled) */
    EVLOG_GAMEOVER,          /* a=0 life reached zero, 1 subdispatch 6 */
    EVLOG_DEATH_WINDOW,      /* zyouout id 9: a=1 from op 0x03 (Timer2 off
                                first); the observer runs timer2on */
    EVLOG_TITLE,             /* a=1 for the R variant */
    EVLOG_PLAYER_UPDATE,
    EVLOG_PLAYER_ANIMATION,
    EVLOG_CHANGE_FLOOR,      /* a=floor */
    EVLOG_OPENING9,          /* a=phase b=frame c=interval set (0 = none) */
    EVLOG_HIDE_MESSAGE,
    EVLOG_HIDE_IMAGES,
    EVLOG_FADE,              /* 0x0b/0xf (0x4539be): the form's canvas
                                copied into 0x48c528, openning9's backdrop */
    EVLOG_MONSTER_MOVE,      /* a=from b=to c=raw moved */
    EVLOG_DOOR_OPEN,         /* a=index b=kind c=tile written (-1 none) d=progress */
    EVLOG_BATTLE_HIT,        /* a,b=current args c,d=previous args */
    EVLOG_STOP_BGM,
    EVLOG_SET_STATE,         /* a=index b=value */
    EVLOG_SET_STATE_OOB,     /* a=index b=value */
    EVLOG_DRAW_ACTOR,        /* a=mode b=map index c=chained */
    EVLOG_TIMERIN,
    EVLOG_RESET,
    EVLOG_SUBDISPATCH_UNKNOWN, /* a=sub b=arg2 */
    EVLOG_UNKNOWN_OPCODE,    /* a=opcode b=arg1 c=arg2 */
    EVLOG_TILE_OOB,          /* a=floor b=index c=value */
    EVLOG_OVERFLOW,          /* append past capacity */
    EVLOG_TIMER1             /* a=enabled: op 0x15's last reveal step turns
                                Timer1 (form+0x1b4) on again (0x4547c4) */
} EventVmLogKind;

typedef struct {
    EventVmLogKind kind;
    int32_t a, b, c, d;
} EventVmLog;

typedef struct {
    uint16_t opcode;
    uint16_t arg1;
    uint16_t arg2;
} EventCommand;

typedef struct {
    int32_t hp;
    int32_t attack;
    int32_t defense;
    int32_t gold;
} EventMonster;

struct EventVm;

/* Called for opcode 0x0a (kind = EVOP_START_SUBEVENT) and for the splice
 * subcommands of 0x0b (kind = EVOP_SUBDISPATCH).  For splices the count has
 * already been decremented; anything the callback appends overwrites the
 * current 0x0b command. */
typedef void (*EventVmSubFn)(struct EventVm *vm, int kind, int sub,
                             int arg2, void *user);
typedef void (*EventVmObserverFn)(struct EventVm *vm, const EventVmLog *entry,
                                  void *user);

typedef struct EventVm {
    StateBlock *state;
    MapBlock *map;
    EventCommand commands[EVENT_VM_CAPACITY];
    int count;
    EventMonster monster;
    /* g_monsters[0x0f] (Zeno, 0x489a00..08) is rewritten at run time:
     * syokidata2 (event_vm_syokidata2_zeno), handan on floor 50, the
     * floor-24 and floor-49 one-shots.  hp, attack, defense. */
    int32_t zeno[3];
    int32_t monster_hp;
    int monster_index;
    int current_sprite;
    int anim_progress;     /* DAT_0048c558, set by op 0x0d from prev arg2 */
    int speed;             /* EVM_SPEED_* (option.dat byte 3) */
    int interval_ms;       /* current Timer2 interval */
    bool gameover;
    bool overflow;
    EventVmSubFn sub_fn;
    void *sub_user;
    EventVmObserverFn observer;
    void *observer_user;
} EventVm;

static inline int event_vm_s16(uint16_t v)
{
    return (v & 0x8000) ? (int)v - 0x10000 : (int)v;
}

/* syokidata2 (0x454de8, on load / new game / game over): Zeno's table
 * entry from state[0x9c] -- 0: 8000/5000/1000, 1: (difficulty + 1) x
 * 800/500/100, otherwise unchanged. */
void event_vm_syokidata2_zeno(EventVm *vm);
void event_vm_init(EventVm *vm, StateBlock *state, MapBlock *map);
/* Tile written by `dooropen` for a door kind (arg2), or -1 for none. */
int event_vm_door_tile(int kind);
/* Drop all pending commands (count = 0) and clear transient VM state. */
void event_vm_clear(EventVm *vm);
/* Install a command list in original append order (last executes first). */
bool event_vm_load(EventVm *vm, const EventCommand *cmds, int n);
/* Append one command (handler side).  Returns false on overflow. */
bool event_vm_append(EventVm *vm, int opcode, int arg1, int arg2);

/* zyouout with g_subevent_id 9 (0x449e18): the game-over timeline.  With
 * the BGM on (state[0x5a] != 0) it becomes 0x0e and the death jingle
 * (op 0x01/5) plays first; then op 0x0a/0x5d asks "try again?" and op
 * 0x16 resets.  The caller turns Timer2 on (timer2on). */
void event_vm_death_timeline(EventVm *vm);
bool event_vm_active(const EventVm *vm);

/* Execute the command at the cursor and decrement it.  Returns false when
 * there is nothing to execute. */
bool event_vm_step(EventVm *vm);
/* Run until the count drains or max_steps; returns steps executed. */
int event_vm_run(EventVm *vm, int max_steps);
/* One `timer2timer` invocation: at most EVENT_VM_STEPS_PER_TICK steps. */
int event_vm_tick(EventVm *vm);

uint8_t event_vm_tile(const EventVm *vm, int floor, int index);
void event_vm_set_tile(EventVm *vm, int floor, int index, int value);

#endif
