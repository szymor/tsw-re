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
 * parity.  docs/re/12-event-vm.md sections 11 and 13,
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
/* Sounds the engine asks for, in order (event_engine_take_audio).  The
 * original's switches are already applied: WAV sounds only while
 * state[0x59] is 0, the BGM only while state[0x5a] (the tune's id) is not
 * 0. */
enum {
    EVENT_AUDIO_WAV = 1,         /* arg: op 0x01's index, 1..4 or 7..10 */
    EVENT_AUDIO_BGM,             /* arg: tune id 5..0x15 (soundplay) */
    EVENT_AUDIO_BGM_STOP,        /* MediaPlayer5.Stop */
};
typedef struct {
    int kind;
    int arg;
} EventAudioCmd;
#define EVENT_ENGINE_AUDIO_QUEUE 32

typedef struct {
    bool active;
    int floor;          /* floor it was drawn on */
    int map_index;      /* op 0x09 arg1 */
    int background;     /* previous command's arg1 (e.g. 6 = floor) */
    int sprite;         /* previous command's arg2 (e.g. 0x5d sergeant) */
    int mask;           /* op 0x09 arg2 (dissolve frame / sprite mask) */
    /* The map buffers still holding it: bit 0 = 0x48c514 (frame A), bit 1
     * = 0x48c518 (frame B, background and sprite + imagework's offset).
     * Op 0x09 draws both; Timer1's hero redraw clears one at a time. */
    unsigned buffers;
    /* On the form now: set when op 0x09 blits 0x48c514, and at each Timer1
     * tick from the buffer it shows (before its hero redraw). */
    bool on_screen;
} EventVmActor;

/* Timer1 (form+0x1b4, Timer1Timer 0x443120, the speed interval):
 * mhyouji keeps every tile in two buffers, 0x48c514 with tile - 1 and
 * 0x48c518 with tile - 1 + imagework's offset, and each tick shows the
 * buffer 0x48c5d2 names, then toggles it.  Outside battle rendering
 * (state[0xc] == 0) the tick then redraws the hero standing in the buffer
 * it just showed (yusyahyouji, ImageList 0xbc + 8 * (facing - 1) through
 * the next tile), over whatever op 0x09 composed at his cell.  After a
 * step yusyahyouji1 put the walking poses 0xb8 (frame B) / 0xba (frame A)
 * in both, so a step shows both, then the standing pose. */
enum { EVENT_HERO_WALK_B = 0, EVENT_HERO_WALK_A = 1, EVENT_HERO_STAND = 2 };
/* imagework (0x454c10): the frame-B offset of map value `raw` (state[0x74],
 * 0x4b8858, set by the floor-24 event, stops 0x1b/0x1c animating). */
int event_engine_anim_offset(int raw, int32_t state_74);
/* The 1-based tile mhyouji (0x442c38) draws for map value `value` in frame
 * A (`frame_b` false: the value itself) or frame B (plus the imagework
 * offset); the ImageList index is one less.  0 draws nothing. */
int event_engine_map_tile(int value, bool frame_b, int32_t state_74);
/* 1-based Chtable tile of the hero for facing 1..4 and a pose above (the
 * mask is the next tile). */
int event_engine_hero_tile(int facing, int pose);

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

/* The floor-change wipe (kaidanwork's two op-0x15 passes): cells in the
 * order of the table at 0x489b20 (a spiral from the top-left corner down
 * the left column and inwards) are first covered with ImageList tile
 * 0x9e (mode 0), then redrawn from the new floor (mode 1).  Until a cell
 * is covered it still shows the floor that was left. */
typedef struct {
    bool active;
    uint8_t cell[121];       /* 0 old floor, 1 covered, 2 new floor */
    uint8_t old_tiles[121];  /* the floor that was left */
    int old_floor, old_player, old_facing;
} EventWipe;

/* Map index of draw step n (1..121): the order table at 0x489b1f + n. */
int event_engine_wipe_cell(int n);

/* openning9 (op 0x0b/0xc) picture: 1 open0, 2 open1, 3 open2.
 * Phases 0 and 2 (0x482e60) dissolve it in: each frame composes, over the
 * whole 640x400 form, R = (B | ~M) & (S | M) (kasanegaki) with S the
 * picture, M a 20x13 grid of the mask tile ImageList[frame - 0x42] and B
 * the bitmap 0x48c528 -- the three bare panel pictures syokidata2 copies
 * there (no map, no texts), or the screen sub-dispatch 0xf captured (the
 * floor-50 endings, phase 2).  Phase 0's first frame (0xe8) also hides the
 * panels, so the map and everything on it vanish at once. */
typedef struct {
    int image;          /* 0 = none */
    int overlay;        /* image revealed over it from the top, or 0 */
    int reveal;         /* revealed rows (0..400) of `overlay` */
    int dissolve;       /* 1-based Chtable mask tiled over `image`, 0 none */
    /* The floor-50 ending's pictures, openning9 phases 4..7 (0 = none):
     *   6  frame 1 open0, frames 2..0x11 End05..End20 (450 ms each);
     *   4  End20 shrinking to the centre over white (CopyMode WHITENESS,
     *      then a stretched CopyRect into event_engine_ending_shrink());
     *   5  End26 (plain white) with the form's Image21 (the closing text) revealed from
     *      the top, `ending_frame` rows (at most 400);
     *   7  Image23 shows the form's Image33 ("The End"), its hint "Thank
     *      you for playing!"; phase 8 then shows Panel2/Image34 on top. */
    int ending_phase;
    int ending_frame;
} EventCutscene;

/* Phase 6's picture for `frame`: 0 open0, 5..20 EndNN, -1 none. */
int event_engine_ending_picture(int frame);
/* Phase 4's destination rectangle {left, top, right, bottom} for `frame`
 * (0x483c4f: left = (frame * 640 / 400) / 2, top = frame / 2, right =
 * 640 - top * 640 / 400, bottom = 400 - top). */
void event_engine_ending_shrink(int frame, int rect[4]);

/* 1-based Chtable mask of openning9 frame `frame` (ImageList index
 * frame - 0x42): 0xe8..0xf7 give 0xa7..0xb6, white to black. */
int event_engine_dissolve_mask(int frame);

/* banpaiawork (0x0b/4, floor 20): eight bats converge on the 3x3 block at
 * map cells (4..6, 4..6) while the vampire fades in; `stage` is the bats'
 * offset in px (0..32).  Drawn into the map buffer, so it lasts until one of
 * those cells is redrawn. */
typedef struct {
    bool active;
    int floor;
    int stage;
} EventVampire;

/* The right panel's monster box as monshyouji (0x44b78c) fills it:
 * Image31 (561,230) shows the monster through its mask over panel tile
 * 0xa2, Memo8 (524,268) the name monswork chose (ListBox2[index + 0x5e],
 * 0x5d "The Name" for index -1), Memo5..7 (557,288/308/328) life,
 * offense and defense times (difficulty + 1), right-justified.  With no
 * monster (index -1: syokidata2, the stairs, the Orb of Flying) Image31 is
 * hidden and the memos hold ListBox2[0xd6..0xd8], left-justified.  Op 0x05
 * and op 0x0e write the monster's remaining life into Memo5. */
typedef struct {
    int index;          /* g_monster_index (0x48c5b8), -1 = none */
    int32_t life, attack, defense;
} EventMonsterPanel;

/* 0-based ImageList1/ImageList3 index of monster `index`'s picture
 * (monshyouji), or -1. */
int event_engine_monster_picture(int index);
/* The same picture in the map's frame B when frame_b (imagework's offset,
 * as the map animates the monster's tile), else frame A; -1 for none. */
int event_engine_monster_picture_frame(int index, bool frame_b);
/* ListBox2 index of the panel's name (monswork). */
int event_engine_monster_name_id(int index);

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
    /* Who speaks the window's text: its first entry's speaker column
     * (messages.h), as speaker + 1; 0 = nobody (narration, status). */
    int dialog_speaker;
    SpeakerNames speaker_names;   /* data/text/<lang>/speakers.tsv */
    /* Button1.Caption: "OK" (the form's) until an event sets it, e.g.
     * ListBox2[0xdb] "Hey!" (openning2) or [0xda] "What?" (mevent,
     * ichicheck).  It stays until the next event changes it. */
    char button_caption[64];
    int unported;                 /* sub-events/confirms with no builder */
    /* idou (0x44c04c) signalled its player step; the caller starts the
     * tile slide from the pre-trigger position. */
    bool player_step_pending;
    EventVmActor actors[EVENT_ENGINE_MAX_ACTORS];
    EventCutscene cutscene;
    EventWipe wipe;
    EventVampire vampire;
    EventMonsterPanel monster_panel;
    /* Timer1 (see EVENT_HERO_*): enabled, the buffer on the form (0 frame
     * A, 1 frame B), the one the next tick shows (0x48c5d2) and the hero's
     * pose in each buffer. */
    bool timer1;
    int shown_buffer;
    int timer1_next;
    int hero_pose[2];
    int hero_shown_pose;          /* the pose in the last blit */
    /* mhyouji (0x442c38) redrew the floor's tiles over the hero, and no
     * hero redraw has happened since (yusyahyouji, or Timer1 while
     * state[0xc] is 0): the Orb of Flying's floors show no hero. */
    bool hero_erased;
    /* Image1's picture (the center panel): 0 CENTER.BMP, 1 title05.bmp,
     * 2 title05R.bmp.  Sub-dispatch 7 (0x45345c) loads the title, which
     * covers the map until openning9 hides the panels. */
    int center_picture;
    /* Sub-dispatch 0xe (0x45371e) hid the panels' values: the stat and
     * monster memos (+0x3c8..0x3f0), the weapon/shield and monster
     * pictures and the item images of slots 3..14; the orbs (slots 0..2)
     * stay.  openning9 phase 1's last frame and syokidata2 show them. */
    bool hud_hidden;
    /* Screens captured into 0x48c528 by sub-dispatch 0xf since the last
     * syokidata2 (0: the bitmap holds the bare panels). */
    int screen_captures;
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
    bool status_fresh;            /* a text chosen since the last disp */
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
    /* The area shown (BitBtn1..5: browse the other areas' tables, read
     * from their files; ranking stays the fairy's). */
    int ranking_view_area;
    Ranking ranking_view;
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
    char death_prompt_text[EVENT_ENGINE_TEXT_LEN];  /* data/text/en/ui.tsv 1 */
    /* Literals of the code (data/text/en/ui.tsv, tools/extract_messages.py). */
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
    /* The Orb of Wisdom's window (img5work): Button11..34 (enabled when
     * the speaker's flag is 2) and Button35 (OK).  A button shows the old
     * man's or merchant's words in RichEdit1 without Button1.  wisdom_focus: the
     * highlighted button 0..23, -1 when none is enabled. */
    bool wisdom_open;
    bool wisdom_enabled[24];
    int wisdom_focus;
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
    EventAudioCmd audio[EVENT_ENGINE_AUDIO_QUEUE];
    /* Options > Background Music On (form+0x330 checked); the app sets
     * it.  gameover restarts the floor's tune with it. */
    bool bgm_switch;
    int audio_count;
    EventEngineLogFn log_fn;
    void *log_user;
} EventEngine;

/* Load builders.lua.  On failure the engine stays usable but routes
 * nothing (legacy events keep working). */
bool event_engine_init(EventEngine *e, const char *builders_path,
                       char *err, size_t errsz);
/* Load data/text/en/messages.tsv (tools/extract_messages.py).  Without it the
 * dialogue shows "[message N]" placeholders. */
bool event_engine_load_messages(EventEngine *e, const char *path,
                                char *err, size_t errsz);
void event_engine_shutdown(EventEngine *e);
/* Point the VM at the live game state/map (call after new game/load too). */
void event_engine_bind(EventEngine *e, StateBlock *state, MapBlock *map);

/* Move up to `max` queued sound requests to `out`; returns how many. */
int event_engine_take_audio(EventEngine *e, EventAudioCmd *out, int max);
/* soundcheck + soundplay as loadwork / syokidata2 do: with the BGM on
 * (state[0x5a] != 0) pick the floor's tune and queue it; else queue a
 * stop. */
void event_engine_floor_bgm(EventEngine *e);

/* Builder name routed for a map trigger, or NULL. */
const char *event_engine_route(int floor, int index, uint8_t raw);

EventEngineResult event_engine_trigger(EventEngine *e, int floor, int index,
                                       uint8_t raw, int direction);
/* One Timer1 tick (the speed interval; nothing while Timer1 is off): the
 * shown map buffer, the hero redraw and the floor-15/35 visit counters. */
void event_engine_timer1(EventEngine *e);
/* Timeline running, dialogue waiting for confirm, or a hand-over pending. */
bool event_engine_active(const EventEngine *e);
/* Advance the VM to time `now` (ms).  Returns true when something the
 * caller must react to happened: a hand-over is pending or the timeline
 * drained. */
bool event_engine_update(EventEngine *e, uint64_t now);
/* Text of the visible dialogue window, or NULL. */
const char *event_engine_dialog(const EventEngine *e);
/* Caption of the dialogue's confirm button (Button1). */
const char *event_engine_button_caption(const EventEngine *e);
/* The speaker of the shown dialogue (messages.h speaker_def), or -1. */
int event_engine_dialog_speaker(const EventEngine *e);
/* That speaker's name in the text language, or NULL. */
const char *event_engine_speaker_name(const EventEngine *e, int speaker);
/* Load data/text/<lang>/speakers.tsv over the English names. */
bool event_engine_load_speakers(EventEngine *e, const char *path);
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
/* The Orb of Wisdom's window: press button k (0..23, Button11+k) or OK
 * (k = 24, Button35).  False if the window is closed or k disabled. */
bool event_engine_wisdom_press(EventEngine *e, int k);
/* Highlight the previous (delta < 0) or next enabled button and show its
 * words (presses it).  The caller presses the first one on opening. */
void event_engine_wisdom_move(EventEngine *e, int delta);

/* Game > Ranking (HighScore1Click 0x48297c): state[0x56] and [0x57]
 * cleared, Form2 shown for `area` (1..5) with no name to enter.  False
 * while an event runs. */
bool event_engine_show_ranking(EventEngine *e, int area);
/* Form2's area buttons: show the area `delta` steps from the one shown
 * (1..5, wrapping).  The fairy's own table is not touched. */
bool event_engine_ranking_browse(EventEngine *e, int delta);
/* The table of the area shown. */
const Ranking *event_engine_ranking_shown(const EventEngine *e);

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
