#ifndef TSW_EVENT_BUILDERS_H
#define TSW_EVENT_BUILDERS_H

/* Lua builder runtime for the original event handlers.
 *
 * A builder is a Lua function named after an original handler (`roujin`,
 * `mevent`, `zenolivework`, ...).  Like the original it runs to completion
 * without yielding: it reads state, map and auxiliary globals, applies its
 * builder-side effects and appends 6-byte commands to the EventVm.  The VM
 * then consumes them (docs/design/01-event-system.md section 7).
 *
 * Builders are accepted on golden-vector parity with the native oracle
 * (tests/test_event_parity.c).  A builder that reaches a path that has not
 * been ported calls fail(); the runtime then restores the state, map,
 * auxiliaries and command buffer to their values before the call, so a
 * partial event never leaks into the game (fail closed).
 *
 * The runtime is SDL-free; presentation calls made by builders (`present`)
 * are forwarded to an optional callback.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "event_vm.h"

struct lua_State;

/* Auxiliary globals, named and ordered like native_event_oracle.AUX_ADDRS. */
typedef enum {
    EVAUX_TARGET_INDEX = 0,   /* 0x48c584 */
    EVAUX_DIRECTION,          /* 0x48c590 */
    EVAUX_SPRITE,             /* 0x48c58c  g_anim_sprite */
    EVAUX_UI_MODE,            /* 0x48c5ce */
    EVAUX_MESSAGE_SELECTOR,   /* 0x48c5a0  g_dialog_selector */
    EVAUX_EVENT_SELECTOR,     /* 0x48c55c  g_event_selector */
    EVAUX_EVENT_SPRITE,       /* 0x48c558 */
    EVAUX_EVENT_PARAM,        /* 0x48c560 */
    EVAUX_ROUJIN_AUX,         /* 0x48c5a4  g_subevent_id */
    EVAUX_ROUJIN_SELECTOR,    /* 0x48c564  g_battle_unwinnable */
    EVAUX_UI_SELECTOR,        /* 0x48c5c8 */
    EVAUX_CURRENT_MONSTER,    /* 0x48c5b8 */
    EVAUX_MONSTER_HP,         /* 0x48c5bc */
    EVAUX_SHOP_PRICE,         /* 0x48c594 */
    EVAUX_SHOP_FLAG,          /* 0x48c598  state index of the bought flag */
    EVAUX_SHOP_TALK_FLAG,     /* 0x48c59c  state index of the talk flag */
    EVAUX_CHOICE_MODE,        /* 0x48c5cc  word: 0 merchant, 1 altar */
    EVAUX_BATTLE_TURNS,       /* 0x48c56c  g_battle_turns */
    EVAUX_CONFIRM_CASE,       /* 0x48c574  item-use confirm case (Button38Click) */
    EVAUX_COUNT
} EventAuxId;

typedef struct {
    int32_t v[EVAUX_COUNT];
} EventAux;

const char *event_aux_name(int id);
int event_aux_find(const char *name);   /* -1 if unknown */

typedef void (*EventPresentFn)(void *user, const char *kind,
                               const int32_t *args, int nargs);

typedef struct EventBuilders {
    struct lua_State *L;
    EventPresentFn present_fn;
    void *present_user;
    /* The area's optionN.dat as BlockRead returns it (72 ints); false when
     * it cannot be read.  NULL: no file (the oracle's zeros). */
    bool (*ranking_fn)(void *user, int area, int32_t out[72]);
    void *ranking_user;
    /* A builder assigned aux.sprite (g_anim_sprite) since the engine last
     * looked; disp logs its text even when it repeats. */
    bool sprite_set;
    char err[512];
} EventBuilders;

bool event_builders_init(EventBuilders *b, const char *script_path,
                         char *err, size_t errsz);
void event_builders_shutdown(EventBuilders *b);
bool event_builders_has(EventBuilders *b, const char *name);

/* Run builder `name` against vm (state/map/command buffer) and aux.
 * Returns false and fills b->err on failure; all inputs are then restored. */
bool event_builders_run(EventBuilders *b, const char *name, EventVm *vm,
                        EventAux *aux);

/* Point the VM's battle operands (g_monster_index and its stats) at a
 * MONSTER_TABLE entry. */
void event_builders_bind_monster(EventVm *vm, int index);

#endif
