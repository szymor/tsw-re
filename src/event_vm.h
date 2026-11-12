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
    EVLOG_TITLE,             /* a=1 for the R variant */
    EVLOG_PLAYER_UPDATE,
    EVLOG_PLAYER_ANIMATION,
    EVLOG_CHANGE_FLOOR,      /* a=floor */
    EVLOG_OPENING9,
    EVLOG_HIDE_MESSAGE,
    EVLOG_HIDE_IMAGES,
    EVLOG_FADE,
    EVLOG_MONSTER_MOVE,
    EVLOG_DOOR_OPEN,
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
    EVLOG_OVERFLOW           /* append past capacity */
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
    int32_t monster_hp;
    int monster_index;
    int current_sprite;
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

void event_vm_init(EventVm *vm, StateBlock *state, MapBlock *map);
/* Drop all pending commands (count = 0) and clear transient VM state. */
void event_vm_clear(EventVm *vm);
/* Install a command list in original append order (last executes first). */
bool event_vm_load(EventVm *vm, const EventCommand *cmds, int n);
/* Append one command (handler side).  Returns false on overflow. */
bool event_vm_append(EventVm *vm, int opcode, int arg1, int arg2);
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
