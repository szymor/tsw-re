#ifndef TSW_EVENT_ENGINE_H
#define TSW_EVENT_ENGINE_H

/* Event engine: original-handler builders + command VM + the dialogue
 * window, driven like the original's Timer2.  SDL-free so it can be
 * unit-tested.
 *
 *   trigger  -> builder (Lua, src/events/builders.lua) appends commands and
 *               applies builder-side effects to the game state/map;
 *   Timer2   -> while enabled and commands remain, the VM consumes up to two
 *               commands per tick (timer2timer 0x45265c) at the recovered
 *               interval (event_vm.h).  timer2on (0x44bc0c) enables it;
 *   op 0x0a  -> sets g_subevent_id = arg1 + 10, disables Timer2 and opens
 *               the zyouout window (0x449e18) with text from ListBox2;
 *   confirm  -> the `button1click` builder (Button1Click 0x44da10) runs:
 *               marks state[0x50 + id], may append a timeline and re-enable
 *               Timer2.  A confirm case that is not ported yet hands its
 *               "subevent:<id>" key to the transitional coroutine runtime;
 *   0x0b     -> splice sub-handlers run their builders (openning31,
 *               zenolivework, ...); openning9 (0x0b/0xc) frames drive the
 *               cutscene picture and phase 8 stops Timer2.
 *
 * Only triggers listed in the route table are sent to builders; everything
 * else keeps the legacy (floor, index, raw) Lua path until it is ported and
 * passes golden-vector parity.  docs/design/01-event-system.md section 7,
 * docs/re/12-event-vm.md section 11, docs/re/18-messages.md.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "event_builders.h"
#include "event_vm.h"
#include "messages.h"

#define EVENT_ENGINE_KEY_LEN 64
#define EVENT_ENGINE_TEXT_LEN 1024

#define EVENT_ENGINE_MAX_ACTORS 8

/* Presentation actor driven by VM opcodes (0x09 three-sprite animation):
 * one per map index (Zeno, the four Magic Sergeants, ...). */
typedef struct {
    bool active;
    int floor;          /* floor it was drawn on */
    int map_index;
    int atlas_sprite;   /* Chtable atlas index */
} EventVmActor;

/* openning9 (op 0x0b/0xc) picture: 1 open0, 2 open1, 3 open2. */
typedef struct {
    int image;          /* 0 = none */
    int overlay;        /* image revealed over it from the top, or 0 */
    int reveal;         /* revealed rows (0..400) of `overlay` */
} EventCutscene;

typedef enum {
    EVENT_ENGINE_NOT_ROUTED = 0,  /* no builder for this trigger */
    EVENT_ENGINE_STARTED = 1,     /* builder ran; VM may be active */
    EVENT_ENGINE_FAILED = -1      /* builder failed closed; inputs restored */
} EventEngineResult;

typedef void (*EventEngineLogFn)(void *user, const char *message);

typedef struct EventEngine {
    EventBuilders builders;
    bool have_builders;
    MessageTable messages;
    bool have_messages;
    EventVm vm;
    EventAux aux;
    bool timer2;                  /* Timer2 (form+0x2ec) enabled */
    bool ticking;
    uint64_t next_tick;
    /* zyouout window (RichEdit1 +0x1cc) and its OK button (Button1). */
    bool dialog_visible;
    bool dialog_open;             /* waiting for the confirm */
    int dialog_id;                /* g_subevent_id when it opened */
    char dialog_text[EVENT_ENGINE_TEXT_LEN];
    char continuation[EVENT_ENGINE_KEY_LEN];
    EventVmActor actors[EVENT_ENGINE_MAX_ACTORS];
    EventCutscene cutscene;
    EventEngineLogFn log_fn;
    void *log_user;
} EventEngine;

/* Load builders.lua.  On failure the engine stays usable but routes
 * nothing (legacy events keep working). */
bool event_engine_init(EventEngine *e, const char *builders_path,
                       char *err, size_t errsz);
/* Load data/text/messages.tsv (tools/extract_messages.py).  Without it the
 * dialogue shows "[message N]" placeholders. */
bool event_engine_load_messages(EventEngine *e, const char *path,
                                char *err, size_t errsz);
void event_engine_shutdown(EventEngine *e);
/* Point the VM at the live game state/map (call after new game/load too). */
void event_engine_bind(EventEngine *e, StateBlock *state, MapBlock *map);

/* Builder name routed for a map trigger, or NULL. */
const char *event_engine_route(int floor, int index, uint8_t raw);

EventEngineResult event_engine_trigger(EventEngine *e, int floor, int index,
                                       uint8_t raw, int direction);
/* Timeline running, dialogue waiting for confirm, or a hand-over pending. */
bool event_engine_active(const EventEngine *e);
/* Advance the VM to time `now` (ms).  Returns true when something the
 * caller must react to happened: a hand-over is pending or the timeline
 * drained. */
bool event_engine_update(EventEngine *e, uint64_t now);
/* Text of the visible dialogue window, or NULL. */
const char *event_engine_dialog(const EventEngine *e);
/* The player confirms the open dialogue (Button1Click).  Returns false if
 * no dialogue was waiting. */
bool event_engine_confirm(EventEngine *e);
/* VM actor drawn at map index i of the current floor, or NULL. */
const EventVmActor *event_engine_actor_at(const EventEngine *e, int floor,
                                          int map_index);
/* Hide all VM actors (floor change, timeline finished). */
void event_engine_clear_actors(EventEngine *e);
/* Take the pending transitional key (empty string if none). */
bool event_engine_take_continuation(EventEngine *e, char *out, size_t outsz);
/* Drop the timeline, auxiliaries, dialogue, actor and continuation
 * (load/new game). */
void event_engine_cancel(EventEngine *e);

#endif
