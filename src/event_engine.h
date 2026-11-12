#ifndef TSW_EVENT_ENGINE_H
#define TSW_EVENT_ENGINE_H

/* Event engine: original-handler builders + command VM, driven by a
 * Timer2-equivalent tick.  SDL-free so it can be unit-tested.
 *
 *   trigger  -> builder (Lua, src/events/builders.lua) appends commands and
 *               applies builder-side effects to the game state/map;
 *   update   -> the VM consumes up to two commands per tick, the tick period
 *               being the recovered Timer2 interval (event_vm.h);
 *   drained  -> if a not-yet-ported sub-event was reached, its transitional
 *               coroutine key is handed back to the legacy runtime.
 *
 * Only triggers listed in the route table are sent to builders; everything
 * else keeps the legacy (floor, index, raw) Lua path until it is ported and
 * passes golden-vector parity.  docs/design/01-event-system.md section 7.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "event_builders.h"
#include "event_vm.h"

#define EVENT_ENGINE_KEY_LEN 64

/* Presentation actor driven by VM opcodes (0x09 three-sprite animation). */
typedef struct {
    bool active;
    int map_index;
    int atlas_sprite;   /* Chtable atlas index */
} EventVmActor;

typedef enum {
    EVENT_ENGINE_NOT_ROUTED = 0,  /* no builder for this trigger */
    EVENT_ENGINE_STARTED = 1,     /* builder ran; VM may be active */
    EVENT_ENGINE_FAILED = -1      /* builder failed closed; inputs restored */
} EventEngineResult;

typedef void (*EventEngineLogFn)(void *user, const char *message);

typedef struct EventEngine {
    EventBuilders builders;
    bool have_builders;
    EventVm vm;
    EventAux aux;
    bool ticking;
    uint64_t next_tick;
    char continuation[EVENT_ENGINE_KEY_LEN];
    EventVmActor actor;
    EventEngineLogFn log_fn;
    void *log_user;
} EventEngine;

/* Load builders.lua.  On failure the engine stays usable but routes
 * nothing (legacy events keep working). */
bool event_engine_init(EventEngine *e, const char *builders_path,
                       char *err, size_t errsz);
void event_engine_shutdown(EventEngine *e);
/* Point the VM at the live game state/map (call after new game/load too). */
void event_engine_bind(EventEngine *e, StateBlock *state, MapBlock *map);

/* Builder name routed for a map trigger, or NULL. */
const char *event_engine_route(int floor, int index, uint8_t raw);

EventEngineResult event_engine_trigger(EventEngine *e, int floor, int index,
                                       uint8_t raw, int direction);
bool event_engine_active(const EventEngine *e);
/* Advance the VM to time `now` (ms).  Returns true when the timeline has
 * drained during this call. */
bool event_engine_update(EventEngine *e, uint64_t now);
/* Take the pending transitional key (empty string if none). */
bool event_engine_take_continuation(EventEngine *e, char *out, size_t outsz);
/* Drop the timeline, auxiliaries, actor and continuation (load/new game). */
void event_engine_cancel(EventEngine *e);

#endif
