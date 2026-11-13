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
 *               Timer2.  A confirm case that is not ported yet is logged
 *               and counted in `unported`;
 *   0x0b     -> splice sub-handlers run their builders (openning31,
 *               zenolivework, ...); openning9 (0x0b/0xc) frames drive the
 *               cutscene picture and phase 8 stops Timer2.
 *
 * Only triggers listed in the route table are sent to builders; every other
 * tile has no event until its handler is ported and passes golden-vector
 * parity.  docs/design/01-event-system.md section 7,
 * docs/re/12-event-vm.md section 11, docs/re/18-messages.md.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "event_builders.h"
#include "event_vm.h"
#include "messages.h"
#include "ranking.h"

#define EVENT_ENGINE_TEXT_LEN 1024

#define EVENT_ENGINE_MAX_ACTORS 8

/* Presentation actor driven by op 0x09 (stackwork 0x452e43): one per map
 * index (Zeno, the four Magic Sergeants, the player pose, ...).  The three
 * values are 1-based Chtable tiles (atlas index = value - 1), composed like
 * kasanegaki (0x44439c): R = (background | ~mask) & (sprite | mask), i.e.
 * the sprite shows where the mask is black and the background where it is
 * white.  The dissolve "frames" 0xb6..0xa6 are such masks. */
typedef struct {
    bool active;
    int floor;          /* floor it was drawn on */
    int map_index;      /* op 0x09 arg1 */
    int background;     /* previous command's arg1 (e.g. 6 = floor) */
    int sprite;         /* previous command's arg2 (e.g. 0x5d sergeant) */
    int mask;           /* op 0x09 arg2 (dissolve frame / sprite mask) */
} EventVmActor;

/* Door animation (op 0x0d, dooropen 0x4770e0): the last frame drawn at a
 * map index.  `progress` is the previous command's arg2 (1..16, from
 * doorstackin); event_engine_door_frame() gives the blits. */
typedef struct {
    bool active;
    int floor, map_index, kind, progress;
} EventDoorAnim;

/* Monster move (op 0x0c, monidou 0x480384).  The original slides the
 * sprite one pixel per iteration inside a single tick but refreshes the
 * screen copy only every 8 pixels, so the move shows as 4 steps of 8 px.
 * That loop blocks the message loop while it draws (32 iterations of
 * several BitBlts), so the next Timer2 tick only comes after it.  The
 * remake models that blocking time as EVENT_ENGINE_MOVE_MS, an estimate:
 * the move is drawn over it and the next tick is held until it ends. */
#define EVENT_ENGINE_MOVE_MS 80
typedef struct {
    bool active;
    int floor, from, to, raw;
    uint64_t start, duration;
} EventMoveAnim;

#define EVENT_ENGINE_MAX_MOVES 4

/* One CopyRect of a door frame, in tile pixels (32x32), from tile `src_tile`
 * (1-based Chtable) onto the frame whose base is `base_tile`. */
typedef struct { int sx, sy, sw, sh, dx, dy; } EventDoorBlit;
typedef struct {
    int base_tile;      /* 1-based Chtable tile filling the cell first */
    int src_tile;       /* 1-based Chtable tile the blits copy from */
    bool masked;        /* kind 6: src drawn through its Chtableb mask */
    int nblits;
    EventDoorBlit blits[4];
} EventDoorFrame;

/* openning9 (op 0x0b/0xc) picture: 1 open0, 2 open1, 3 open2. */
typedef struct {
    int image;          /* 0 = none */
    int overlay;        /* image revealed over it from the top, or 0 */
    int reveal;         /* revealed rows (0..400) of `overlay` */
} EventCutscene;

/* banpaiawork (0x0b/4, floor 20): eight bats converge on the 3x3 block at
 * map cells (4..6, 4..6) while the vampire fades in; `stage` is the bats'
 * offset in px (0..32).  Drawn into the map buffer, so it lasts until one of
 * those cells is redrawn. */
typedef struct {
    bool active;
    int floor;
    int stage;
} EventVampire;

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
    /* Merchant/altar question: Button2 (Yes) / Button7 (No) instead of
     * Button1.  No is disabled for the floor-2 gift. */
    bool dialog_choice;
    bool choice_no_enabled;
    bool choice_focus_no;         /* keyboard focus; Button2 (Yes) first */
    char dialog_text[EVENT_ENGINE_TEXT_LEN];
    int unported;                 /* sub-events/confirms with no builder */
    /* idou (0x44c04c) signalled its player step; the caller starts the
     * tile slide from the pre-trigger position. */
    bool player_step_pending;
    EventVmActor actors[EVENT_ENGINE_MAX_ACTORS];
    EventCutscene cutscene;
    EventVampire vampire;
    EventDoorAnim doors[EVENT_ENGINE_MAX_ACTORS];
    EventMoveAnim moves[EVENT_ENGINE_MAX_MOVES];
    uint64_t now;                 /* time of the tick being run */
    uint64_t hold_until;          /* no tick before this (monidou blocking) */
    /* Status-bar string 0x4b8684 (Edit8, shown by disp while
     * g_anim_sprite == 1), e.g. ListBox2[0x89] for a collapsing wall. */
    char status_text[EVENT_ENGINE_TEXT_LEN];
    /* What Edit8 (the bar under the map) shows: disp (0x44cb34) writes
     * status_text when g_anim_sprite is 1, else ListBox2[sprite + 0x7f].
     * Each new text goes to the log (the remake's console). */
    char status_bar[EVENT_ENGINE_TEXT_LEN];
    bool status_fresh;            /* a new text or action since the last disp */
    /* yousei wrote save0.dat (the caller saves it, then clears this). */
    bool checkpoint_pending;
    /* Ranking form shown modally (Button1Click 0x1a, difficulty 0):
     * Form2's FormShow (FUN_00436c14) loads the area's table (its own
     * check), asks for a name when the fairy ranked the player
     * (InputQuery), inserts the row and writes optionN.dat/optionN1.dat
     * and save0.dat (FUN_00439ce0). */
    bool ranking_open;
    Ranking ranking;
    int ranking_area, ranking_rank;
    int32_t ranking_score;
    bool ranking_name_entry;         /* InputQuery open */
    char ranking_name[RANKING_NAME_LEN];
    /* Where the ranking files live (the save directory); "" = none. */
    char ranking_dir[1024];
    /* save0.dat to be rewritten with state 0x55..0x57 cleared (Form2's
     * copy); the caller writes it, then clears the flag. */
    bool ranking_checkpoint_pending;
    /* ShowMessage: a modal message closed with OK. */
    bool message_open;
    char message_text[EVENT_ENGINE_TEXT_LEN];
    /* The altar's menu (Button39..42 after Button2Click mode 1). */
    bool altar_open;
    int altar_focus;              /* 0..3; Button42 (cancel) first */
    char altar_options[4][128];
    /* zyouout's "try again?" MessageDlg (sub-event >= 10, selector
     * 0x5d, state[0xa7] != 1): Yes -> timer2on (the timeline goes on to
     * op 0x16), No -> the timeline stops and Load is enabled. */
    bool death_prompt;
    char death_prompt_text[EVENT_ENGINE_TEXT_LEN];  /* data/text/ui.tsv 1 */
    /* Literals of the code (data/text/ui.tsv, tools/extract_messages.py). */
    MessageTable ui_texts;
    bool have_ui_texts;
    bool load_enabled;            /* GameLoad1 (form+0x334) enabled */
    /* Item use (img8work..img17work): the Yes/No panel and its confirm
     * case (0x48c574); Button38Click (Yes) / Button37Click (No). */
    bool item_confirm;
    int item_confirm_case;
    bool item_confirm_focus_no;
    /* Orb of Flying (img4work): Button8 up / Button9 down / Button10 land. */
    bool fly_open;
    /* openning9 phase 8: the ending's picture (Image34) waits for a click
     * (image34click -> append), which restarts Timer2. */
    bool ending_wait;
    bool fly_up_enabled, fly_down_enabled;
    /* The Orb of the Hero's / Wisdom's window was opened (the caller shows
     * it, then clears the flag). */
    bool hero_orb_pending;
    bool wisdom_orb_pending;
    /* The Orb of the Hero's rows (yousya): monster index and obudameiji's
     * damage (-1: cannot be hurt), at most seven, in map order. */
    int hero_rows;
    int hero_row_index[7];
    int hero_row_damage[7];
    /* op 0x16 -> gameover: syokidata rewrites the map from this (NULL:
     * the map is left as it is).  `resets` counts completed resets. */
    const MapBlock *initial_map;
    int resets;
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
/* One Timer1 tick (500 ms): the floor-15/35 visit counters. */
void event_engine_timer1(EventEngine *e);
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
/* Close the ranking window.  Timer2 stays as it was (the original does not
 * restart it). */
bool event_engine_close_ranking(EventEngine *e);
/* Pick an altar option: 0 life, 1 attack, 2 defense, 3 cancel
 * (Button39..42Click). */
bool event_engine_altar_choose(EventEngine *e, int option);
/* Answer an open Yes/No question (Button2Click / Button7Click).  Returns
 * false if none is open or No is disabled. */
bool event_engine_choose(EventEngine *e, bool yes);

/* Item overlay Enter on special-item slot 0..14 (formkeydown -> img*work).
 * NOT_ROUTED for the passive slots 3, 10 and 14. */
EventEngineResult event_engine_use_item(EventEngine *e, int slot);
const char *event_engine_item_handler(int slot);
/* Answer the item panel: Yes (Button38Click) or No (Button37Click). */
bool event_engine_item_answer(EventEngine *e, bool yes);
/* Orb of Flying panel: +1 up (Button8), -1 down (Button9), 0 land
 * (Button10).  False if the panel is closed or the button disabled. */
bool event_engine_fly(EventEngine *e, int action);

/* The ranking form's name prompt: OK with `name` (may be empty) or
 * Cancel.  Returns false if no prompt is open. */
bool event_engine_ranking_name(EventEngine *e, const char *name, bool ok);
/* Close a ShowMessage box. */
bool event_engine_close_message(EventEngine *e);

/* disp (0x44cb34): refresh status_bar from aux.sprite / status_text. */
void event_engine_disp(EventEngine *e);

/* Click the ending's picture (image34click -> append). */
bool event_engine_ending_continue(EventEngine *e);

/* Answer the "try again?" prompt (MessageDlg mrYes / mrNo). */
bool event_engine_death_answer(EventEngine *e, bool yes);

/* The map syokidata writes on op 0x16 (data/maps/initial.bin). */
void event_engine_set_initial_map(EventEngine *e, const MapBlock *map);
/* VM actor drawn at map index i of the current floor, or NULL. */
const EventVmActor *event_engine_actor_at(const EventEngine *e, int floor,
                                          int map_index);
/* Hide all VM actors and doors (floor change, timeline finished).  Moves
 * expire by time. */
void event_engine_clear_actors(EventEngine *e);
/* dooropen (0x4770e0) frame for door `kind` (1..12) at `progress` (the
 * 640x400 layout, 32 px tiles).  False for kinds that draw nothing. */
bool event_engine_door_frame(int kind, int progress, EventDoorFrame *out);
/* Drop the timeline, auxiliaries, dialogue and actors (load/new game). */
void event_engine_cancel(EventEngine *e);

#endif
