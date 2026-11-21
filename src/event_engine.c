#include "event_engine.h"

#include "battle.h"
#include "fileio.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void engine_log(EventEngine *e, const char *fmt, const char *a)
{
    if (!e->log_fn)
        return;
    char buf[EVENT_ENGINE_TEXT_LEN + 128];
    snprintf(buf, sizeof(buf), fmt, a ? a : "");
    e->log_fn(e->log_user, buf);
}

/* --------------------------------------------------------------------- */
/* zyouout (0x449e18): fill the dialogue window from ListBox2.            */

/* The joiners the engine puts between the pieces of a built text: ui.tsv
 * 8 the word gap (" "; "" where words are not spaced, as in Japanese) and
 * 9 the sentence break (". "; "。"). */
static const char *joiner(const EventEngine *e, bool sentence)
{
    const char *t = e->have_ui_texts
        ? messages_get(&e->ui_texts, sentence ? 9 : 8) : NULL;
    return t ? t : sentence ? ". " : " ";
}

static void append_message(EventEngine *e, int index)
{
    const char *m = e->have_messages ? messages_get(&e->messages, index) : NULL;
    size_t len = strlen(e->dialog_text);
    if (len >= sizeof(e->dialog_text) - 1)
        return;
    if (len == 0) {              /* the window's speaker: its first entry */
        int sp = e->have_messages
            ? speaker_find(messages_speaker(&e->messages, index)) : -1;
        e->dialog_speaker = sp + 1;
    }
    if (m)
        snprintf(e->dialog_text + len, sizeof(e->dialog_text) - len, "%s", m);
    else
        snprintf(e->dialog_text + len, sizeof(e->dialog_text) - len,
                 "[message %d]", index);
}

static void zyouout(EventEngine *e)
{
    int id = e->aux.v[EVAUX_SUBEVENT_ID];             /* g_subevent_id */
    int sel = e->aux.v[EVAUX_UI_SELECTOR] & 0xffff;  /* word 0x48c5c8 */
    if (id == 9) {
        /* The game-over timeline, then timer2on: no window. */
        event_vm_death_timeline(&e->vm);
        e->timer2 = true;
        return;
    }
    if (id < 10) {
        e->dialog_text[0] = '\0';
        append_message(e, e->aux.v[EVAUX_MESSAGE_SELECTOR] + 0x3e);
    } else {
        /* The fairy's selectors keep the text yousei put in RichEdit1. */
        if (sel != 0x10 && sel != 0x16 && sel != 0x18 && sel != 0x28 &&
            sel != 0x2f) {
            e->dialog_text[0] = '\0';
            append_message(e, sel - 1);
        }
        /* LStrCat (0x403658): these append a second entry. */
        if (sel == 5)
            append_message(e, sel + 0x34);
        else if (sel == 0x11)
            append_message(e, sel + 0x29);
        else if (sel == 0x37)
            append_message(e, sel + 4);
    }
    e->dialog_choice = false;
    e->dialog_id = id;
    /* 0x44a3b8: the losing-battle selector asks "Do you try again?" in a
     * MessageDlg instead of showing RichEdit1/Button1. */
    int a7 = e->vm.state ? e->vm.state->raw[0xa7] : 0;
    if (a7 != 1 && sel == 0x5d && id >= 10) {
        e->death_prompt = true;
        e->choice_focus_no = true;             /* the no-action answer */
        return;
    }
    e->dialog_visible = true;
    /* Button1 only while state[0xa7] is 0: the Orb of Wisdom's buttons
     * (state[0xa7] = 1) show the words alone. */
    e->dialog_open = a7 == 0;
}

/* Button1.Caption := ListBox2[index], or the form's 'OK' (index < 0). */
static void set_button_caption(EventEngine *e, int index)
{
    const char *m = index >= 0 && e->have_messages
                        ? messages_get(&e->messages, index) : NULL;
    if (index < 0)
        m = "OK";
    if (m)
        snprintf(e->button_caption, sizeof(e->button_caption), "%s", m);
    else
        snprintf(e->button_caption, sizeof(e->button_caption),
                 "[message %d]", index);
}

/* --------------------------------------------------------------------- */
/* Sound requests                                                         */

static void audio_push(EventEngine *e, int kind, int arg)
{
    if (e->audio_count >= EVENT_ENGINE_AUDIO_QUEUE) {
        /* Keep the newest: a BGM change matters more than an old click. */
        memmove(e->audio, e->audio + 1,
                sizeof(e->audio[0]) * (EVENT_ENGINE_AUDIO_QUEUE - 1));
        e->audio_count--;
    }
    e->audio[e->audio_count++] = (EventAudioCmd){ kind, arg };
}

static int32_t st_raw(const EventEngine *e, int index)
{
    return e->vm.state ? e->vm.state->raw[index] : 0;
}

/* Op 0x01 (0x452762): 1..4 MediaPlayer1..4 (open, get, taisen, zeno.wav)
 * and 7..10 MediaPlayer6 (3f, 20f, kai, kai2.wav) while state[0x59] is 0;
 * 5 soundplay and 6 MediaPlayer5.Stop while state[0x5a] is not 0. */
static void op_sound(EventEngine *e, int index)
{
    if ((index >= 1 && index <= 4) || (index >= 7 && index <= 10)) {
        if (st_raw(e, 0x59) == 0)
            audio_push(e, EVENT_AUDIO_WAV, index);
    } else if (index == 5) {
        if (st_raw(e, 0x5a) != 0)
            audio_push(e, EVENT_AUDIO_BGM, st_raw(e, 0x5a));
    } else if (index == 6) {
        if (st_raw(e, 0x5a) != 0)
            audio_push(e, EVENT_AUDIO_BGM_STOP, 0);
    }
}

int event_engine_take_audio(EventEngine *e, EventAudioCmd *out, int max)
{
    int n = e->audio_count < max ? e->audio_count : max;
    memcpy(out, e->audio, sizeof(*out) * (size_t)n);
    memmove(e->audio, e->audio + n,
            sizeof(e->audio[0]) * (size_t)(e->audio_count - n));
    e->audio_count -= n;
    return n;
}

void event_engine_floor_bgm(EventEngine *e)
{
    StateBlock *s = e->vm.state;
    if (!s)
        return;
    if (s->raw[0x5a] == 0) {
        audio_push(e, EVENT_AUDIO_BGM_STOP, 0);
        return;
    }
    s->raw[0x5a] = event_vm_floor_bgm(s->raw[ST_FLOOR], s->raw[0x5a]);
    audio_push(e, EVENT_AUDIO_BGM, s->raw[0x5a]);
}

/* --------------------------------------------------------------------- */
/* Callbacks                                                              */

static int current_floor(const EventEngine *e)
{
    return e->vm.state ? e->vm.state->raw[ST_FLOOR] : 0;
}

static void set_actor(EventEngine *e, int map_index, int mask,
                      int background, int sprite)
{
    int floor = current_floor(e);
    int slot = -1;
    for (int i = 0; i < EVENT_ENGINE_MAX_ACTORS; i++) {
        EventVmActor *a = &e->actors[i];
        if (a->active && a->floor == floor && a->map_index == map_index) {
            slot = i;
            break;
        }
        if (!a->active && slot < 0)
            slot = i;
    }
    if (slot < 0)
        slot = 0;                       /* full: reuse the oldest slot */
    e->actors[slot].active = true;
    e->actors[slot].floor = floor;
    e->actors[slot].map_index = map_index;
    e->actors[slot].background = background;
    e->actors[slot].sprite = sprite;
    e->actors[slot].mask = mask;
    e->actors[slot].buffers = 3;        /* both map buffers */
    e->actors[slot].on_screen = true;
    e->shown_buffer = 0;                /* op 0x09 then blits 0x48c514 */
}

int event_engine_anim_offset(int raw, int32_t state_74)
{
    /* imagework (0x454c10), on the 1-based map value. */
    unsigned x = (unsigned)raw;
    if ((int)x < 0x1b) {
        if (x == 0x0d || x == 0x0e) return 0x93;
        if (x == 0x10) return 2;
        if (x >= 0x13 && x <= 0x1a) return 1;
        return 0;
    }
    if ((int)x < 0x6a) {
        if (x - 0x1b < 2) return state_74 != 1 ? 1 : 0;
        if (x - 0x1d < 0x20) return 0;
        if (x - 0x3d < 0x24) return 1;
        if (x - 0x61 < 9) return 9;
        return 0;
    }
    if (x - 0x6a < 9) return 0;
    if (x - 0x73 < 9) return 9;
    if (x - 0x7c < 9) return 0;
    if (x - 0x85 < 0x19) return 1;
    return 0;
}

int event_engine_map_tile(int value, bool frame_b, int32_t state_74)
{
    if (!frame_b || value <= 0)
        return value;
    return value + event_engine_anim_offset(value, state_74);
}

int event_engine_hero_tile(int facing, int pose)
{
    /* 0x48c558 = 0xfa/0xfc/0xfe + 8 * (facing - 1) (yusyahyouji1 and
     * Timer1Timer); ImageList index 0x48c558 - 0x42, 1-based + 1. */
    if (facing < 1 || facing > 4) facing = 1;
    if (pose < 0 || pose > 2) pose = EVENT_HERO_STAND;
    return 0xfa + 2 * pose + 8 * (facing - 1) - 0x41;
}

/* yusyahyouji1 (0x4433a4): the walking poses in both buffers. */
static void hero_walk(EventEngine *e)
{
    e->hero_erased = false;
    e->hero_pose[1] = EVENT_HERO_WALK_B;
    e->hero_pose[0] = EVENT_HERO_WALK_A;
    e->hero_shown_pose = e->hero_pose[e->shown_buffer & 1];
}

/* The original draws an op-0x09 composite into the map buffers, so it lasts
 * until something redraws that tile: op 0x06 (not the silent 0x11), a
 * monster move (0x0c) or a player redraw. */
static void drop_actor(EventEngine *e, int floor, int map_index)
{
    for (int i = 0; i < EVENT_ENGINE_MAX_ACTORS; i++) {
        EventVmActor *a = &e->actors[i];
        if (a->active && a->floor == floor && a->map_index == map_index)
            a->active = false;
    }
}

static void drop_actor_at_player(EventEngine *e)
{
    if (!e->vm.state)
        return;
    const int32_t *s = e->vm.state->raw;
    drop_actor(e, s[ST_FLOOR], s[ST_Y] * MAP_TILE_COLS + s[ST_X]);
}

/* Timer1Timer's hero redraw into buffer `b` only. */
static void drop_actor_at_player_buffer(EventEngine *e, int b)
{
    if (!e->vm.state)
        return;
    const int32_t *s = e->vm.state->raw;
    int p = s[ST_Y] * MAP_TILE_COLS + s[ST_X];
    for (int i = 0; i < EVENT_ENGINE_MAX_ACTORS; i++) {
        EventVmActor *a = &e->actors[i];
        if (a->active && a->floor == s[ST_FLOOR] && a->map_index == p) {
            a->buffers &= ~(1u << b);
            if (!a->buffers)
                a->active = false;
        }
    }
}

static void set_door(EventEngine *e, int map_index, int kind, int progress)
{
    int floor = current_floor(e);
    int slot = -1;
    for (int i = 0; i < EVENT_ENGINE_MAX_ACTORS; i++) {
        EventDoorAnim *d = &e->doors[i];
        if (d->active && d->floor == floor && d->map_index == map_index) {
            slot = i;
            break;
        }
        if (!d->active && slot < 0)
            slot = i;
    }
    if (slot < 0)
        slot = 0;
    /* The last frame (progress 16) equals the tile dooropen wrote, so the
     * map takes over; keeping it would hide whatever moves onto the cell. */
    e->doors[slot] = (EventDoorAnim){ progress < 16, floor, map_index, kind,
                                      progress };
}

static void start_move(EventEngine *e, int from, int to, int raw)
{
    int slot = 0;
    uint64_t oldest = UINT64_MAX;
    for (int i = 0; i < EVENT_ENGINE_MAX_MOVES; i++) {
        EventMoveAnim *m = &e->moves[i];
        if (!m->active || m->start + m->duration <= e->now) {
            slot = i;
            break;
        }
        if (m->start < oldest) {
            oldest = m->start;
            slot = i;
        }
    }
    e->moves[slot] = (EventMoveAnim){ true, current_floor(e), from, to, raw,
                                      e->now, EVENT_ENGINE_MOVE_MS };
    if (e->hold_until < e->now + EVENT_ENGINE_MOVE_MS)
        e->hold_until = e->now + EVENT_ENGINE_MOVE_MS;
}

bool event_engine_door_frame(int kind, int p, EventDoorFrame *out)
{
    const int W = 32, H = 32, hw = W / 2, hh = H / 2;
    memset(out, 0, sizeof(*out));
    EventDoorBlit *b = out->blits;
#define BLIT(l, t, r, bo, dl, dt) \
    (b[out->nblits++] = (EventDoorBlit){ (l), (t), (r) - (l), (bo) - (t), (dl), (dt) })
    switch (kind) {
    case 1: case 2: case 3:         /* halves slide left/right */
        out->base_tile = 6; out->src_tile = kind;
        BLIT(p, 0, hw, H, 0, 0);
        BLIT(hw, 0, W - p, H, hw + p, 0);
        break;
    case 4:                         /* quadrants slide out diagonally */
        out->base_tile = 6; out->src_tile = 4;
        BLIT(p, p, hw, hh, 0, 0);
        BLIT(hw, p, W - p, hh, hw + p, 0);
        BLIT(p, hh, hw, H - p, 0, hh + p);
        BLIT(hw, hh, W - p, H - p, hw + p, hh + p);
        break;
    case 5:                         /* quadrants grow in from the corners */
        out->base_tile = 6; out->src_tile = 4;
        BLIT(hw - p, hh - p, hw, hh, 0, 0);
        BLIT(hw, hh - p, hw + p, hh, W - p, 0);
        BLIT(hw - p, hh, hw, hh + p, 0, H - p);
        BLIT(hw, hh, hw + p, hh + p, W - p, H - p);
        break;
    case 6:                         /* tile 5 slides up, masked (kasanegaki) */
        out->base_tile = 6; out->src_tile = 5; out->masked = true;
        BLIT(0, 2 * p, W, H, 0, 0);
        break;
    case 7:                         /* slides down out of the cell */
        out->base_tile = 6; out->src_tile = 8;
        BLIT(0, 0, W, H - 2 * p, 0, 2 * p);
        break;
    case 8:                         /* rises from the bottom */
        out->base_tile = 6; out->src_tile = 8;
        BLIT(0, 0, W, 2 * p, 0, (hw - p) * 2);
        break;
    case 9:                         /* floor wipes in from the right */
        out->base_tile = 13; out->src_tile = 6;
        BLIT(0, 0, 2 * p, H, (hw - p) * 2, 0);
        break;
    case 10:                        /* ... from the left */
        out->base_tile = 13; out->src_tile = 6;
        BLIT(0, 0, 2 * p, H, 0, 0);
        break;
    case 11:                        /* ... from the top */
        out->base_tile = 13; out->src_tile = 6;
        BLIT(0, 0, W, 2 * p, 0, 0);
        break;
    case 12:                        /* ... from the bottom */
        out->base_tile = 13; out->src_tile = 6;
        BLIT(0, (hh - p) * 2, W, H, 0, (hh - p) * 2);
        break;
    default:
        return false;
    }
#undef BLIT
    /* Drop empty or inverted rectangles (progress past the half tile). */
    int n = 0;
    for (int i = 0; i < out->nblits; i++)
        if (b[i].sw > 0 && b[i].sh > 0)
            b[n++] = b[i];
    out->nblits = n;
    return true;
}

int event_engine_dissolve_mask(int frame)
{
    int tile = frame - 0x41;            /* ImageList (0-based) frame - 0x42 */
    return tile >= 1 && tile <= 11 * 20 ? tile : 0;
}

int event_engine_monster_picture(int index)
{
    /* monshyouji (0x44b78c): the first frame of the monster's two. */
    if (index < 0)
        return -1;
    if (index < 0x12)
        return index * 2 + 0x3c;
    if (index == 0x12)
        return 0x67;                    /* Giant Octopus */
    if (index == 0x13)
        return 0x79;                    /* Dragon */
    if (index - 0x14 < 0x0d)
        return index * 2 + 0x5c;
    return -1;
}

int event_engine_monster_picture_frame(int index, bool frame_b)
{
    int pic = event_engine_monster_picture(index);
    if (pic < 0 || !frame_b)
        return pic;
    /* Pictures are map values - 1; no monster value depends on state[0x74]. */
    return event_engine_map_tile(pic + 1, true, 0) - 1;
}

int event_engine_monster_name_id(int index)
{
    /* monswork (0x44b6f4): ListBox2[0x5d], replaced by [index + 0x5e]
     * below 0x22 (a signed compare, so index -1 gives 0x5d again). */
    return index < 0x22 ? index + 0x5e : 0x5d;
}

/* monshyouji (0x44b78c): the panel for g_monster_index. */
static void monshyouji(EventEngine *e, int index)
{
    EventMonsterPanel *p = &e->monster_panel;
    memset(p, 0, sizeof(*p));
    p->index = index;
    if (index < 0 || index >= MONSTER_COUNT || !e->vm.state)
        return;
    int32_t diff1 = e->vm.state->raw[ST_OPTION_A] + 1;
    const MonsterDef *m = &MONSTER_TABLE[index];
    bool zeno = index == 0x0f;          /* g_monsters[0x0f] is run-time */
    p->life = (zeno ? e->vm.zeno[0] : m->hp) * diff1;
    p->attack = (zeno ? e->vm.zeno[1] : m->attack) * diff1;
    p->defense = (zeno ? e->vm.zeno[2] : m->defense) * diff1;
}

static void opening9(EventEngine *e, int phase, int frame)
{
    EventCutscene *c = &e->cutscene;
    switch (phase) {
    case 0:                             /* open1 frames 0xe8..0xf7 */
    case 2:                             /* open0 (the floor-50 endings) */
        memset(c, 0, sizeof(*c));
        c->image = phase == 0 ? 2 : 1;
        c->dissolve = event_engine_dissolve_mask(frame);
        break;
    case 1:                             /* open2 revealed over open1 */
        if (frame == 400 || frame == 500) {
            memset(c, 0, sizeof(*c));   /* panels restored */
            e->hud_hidden = false;
        } else {
            memset(c, 0, sizeof(*c));
            c->image = 2; c->overlay = 3;
            c->reveal = frame < 0 ? 0 : frame > 400 ? 400 : frame;
        }
        break;
    case 4: case 5: case 6: case 7:     /* the ending's pictures */
        memset(c, 0, sizeof(*c));
        c->ending_phase = phase;
        c->ending_frame = frame;
        if (phase == 5 && frame >= 0x190) {
            /* The verse is fully revealed; the original runs straight on
             * to "The End".  The remake waits for Enter (decision 22). */
            e->ending_verse_wait = true;
            e->ending_verse_at = e->now;
            e->timer2 = false;
        } else if (phase == 7) {
            /* "The End": the button follows after a short hold. */
            e->ending_button_at = e->now + EVENT_ENGINE_ENDING_BUTTON_MS;
        }
        break;
    case 8:
        /* Panel2 and Image34 shown, TTimer.SetEnabled(Timer2, False). */
        e->timer2 = false;
        e->ending_wait = true;
        break;
    default:
        break;
    }
}

int event_engine_ending_picture(int frame)
{
    /* openning9 phase 6 (0x4840c5): 1 data\open0, 2..0x11 data2\End05..20. */
    if (frame == 1)
        return 0;
    if (frame >= 2 && frame <= 0x11)
        return frame + 3;
    return -1;
}

void event_engine_ending_shrink(int frame, int rect[4])
{
    int top = frame / 2;                /* SAR with the sign adjustment */
    int left = (frame * 0x280 / 400) / 2;
    rect[0] = left;
    rect[1] = top;
    rect[2] = 0x280 - top * 0x280 / 400;
    rect[3] = 400 - top;
}

/* gameover (0x454da0) after op 0x16 cleared the option words: syokidata
 * (state + embedded initial map), syokidata2 (g_subevent_id 0, g_move_dir
 * 1, g_anim_sprite 0, every window and sprite hidden; the VM already set
 * g_tl_count to 0), itemlive and Timer1 on. */
/* syokidata2: CENTER.BMP back in Image1, Timer1 on (gameover, new game,
 * load), the hero standing in both buffers. */
static void timer1_reset(EventEngine *e)
{
    e->timer1 = true;
    e->shown_buffer = 0;
    e->timer1_next = 0;
    e->hero_pose[0] = e->hero_pose[1] = EVENT_HERO_STAND;
    e->hero_shown_pose = EVENT_HERO_STAND;
    e->hero_erased = false;
    e->floor_traversal = false;
    e->ending_verse_wait = false;
    e->ending_verse_at = 0;
    e->ending_button_at = 0;
    e->center_picture = 0;
}

static void game_over_reset(EventEngine *e)
{
    e->vm.count = 0;                    /* syokidata2: g_tl_count = 0 */
    if (e->vm.state) {
        state_syokidata(e->vm.state);
        event_vm_syokidata2_zeno(&e->vm);
        /* gameover (0x454dd9): with Options > Background Music On checked,
         * soundcheck and soundplay the floor's tune (syokidata cleared
         * state[0x5a]). */
        if (e->bgm_switch) {
            e->vm.state->raw[0x5a] = 5;
            event_engine_floor_bgm(e);
        }
    }
    if (e->vm.map && e->initial_map)
        *e->vm.map = *e->initial_map;
    e->aux.v[EVAUX_SUBEVENT_ID] = 0;
    e->aux.v[EVAUX_DIRECTION] = 1;
    e->aux.v[EVAUX_SPRITE] = 0;
    e->battle_stage = false;
    e->dialog_visible = false;
    e->dialog_open = false;
    e->dialog_choice = false;
    e->altar_open = false;
    e->death_prompt = false;
    e->ending_wait = false;
    e->wisdom_open = false;
    e->status_text[0] = '\0';
    event_engine_clear_actors(e);
    memset(e->moves, 0, sizeof(e->moves));
    memset(&e->cutscene, 0, sizeof(e->cutscene));
    monshyouji(e, -1);                  /* syokidata2 (0x4558de) */
    e->screen_captures = 0;             /* syokidata2 recomposes 0x48c528 */
    e->hud_hidden = false;
    timer1_reset(e);                    /* gameover: Timer1 on */
    e->resets++;
}

int event_engine_wipe_cell(int n)
{
    /* The 121-byte table at 0x489b20: a spiral starting at (0,0), down
     * the left column, right along the bottom, up the right column, left
     * along the top, then the same one ring further in, ending at the
     * centre (60). */
    static int order[121];
    static bool built;
    if (!built) {
        int k = 0, lo = 0, hi = 10;
        while (lo <= hi) {
            for (int y = lo; y <= hi; y++) order[k++] = y * 11 + lo;
            for (int x = lo + 1; x <= hi; x++) order[k++] = hi * 11 + x;
            for (int y = hi - 1; y >= lo && hi > lo; y--) order[k++] = y * 11 + hi;
            for (int x = hi - 1; x > lo; x--) order[k++] = lo * 11 + x;
            lo++; hi--;
        }
        built = true;
    }
    return (n >= 1 && n <= 121) ? order[n - 1] : -1;
}

static void wipe_step(EventEngine *e, int mode, int n)
{
    int c = event_engine_wipe_cell(n);
    if (c < 0 || !e->wipe.active)
        return;
    e->wipe.cell[c] = mode == 0 ? 1 : 2;
    if (mode == 1 && n == 0x79)
        e->wipe.active = false;           /* the new floor is complete */
}

static void on_vm_log(EventVm *vm, const EventVmLog *entry, void *user)
{
    (void)vm;
    EventEngine *e = user;
    switch (entry->kind) {
    case EVLOG_THREE_SPRITE_ANIM:       /* op 0x09 at map index a */
    case EVLOG_BATTLE_HIT:              /* op 0x0e: the same composite
                                           (0x453a36 inlines kasanegaki) */
        set_actor(e, entry->a, entry->b, entry->c, entry->d);
        break;
    case EVLOG_SOUND:
        op_sound(e, entry->a);
        break;
    case EVLOG_STOP_BGM:                /* op 0x0f (0x454038): stop, then
                                           soundcheck + soundplay */
        audio_push(e, EVENT_AUDIO_BGM_STOP, 0);
        if (entry->a != 0)
            audio_push(e, EVENT_AUDIO_BGM, entry->a);
        break;
    case EVLOG_OPENING9:                /* Timer1 off (0x482e7e) first */
        /* Phase 7 ("The End", 0x484693): b_014xgw.mid with the BGM on. */
        if (entry->a == 7 && e->cutscene.ending_phase != 7 &&
            st_raw(e, 0x5a) != 0)
            audio_push(e, EVENT_AUDIO_BGM, 0x14);
        e->timer1 = false;
        e->center_picture = 0;          /* Image1 hidden / CENTER.BMP */
        opening9(e, entry->a, entry->b);
        break;
    case EVLOG_TITLE:                   /* 0x0b/7 (0x45345c): Timer1 off,
                                           title05(R).bmp into Image1 */
        e->timer1 = false;
        e->center_picture = entry->a ? 2 : 1;
        break;
    case EVLOG_HIDE_MESSAGE:            /* 0x0b/0xd (0x45370a): Timer1 off */
        e->timer1 = false;
        break;
    case EVLOG_TIMER1:                  /* op 0x15's last reveal step */
        e->timer1 = entry->a != 0;
        break;
    case EVLOG_SPRITE:                  /* op 0x02: g_anim_sprite, disp */
        if (entry->a) {
            e->aux.v[EVAUX_SPRITE] = entry->a;
            e->status_fresh = true;     /* a text chosen again repeats */
        }
        event_engine_disp(e);
        break;
    case EVLOG_DRAW_ACTOR:              /* op 0x15: one wipe cell */
        wipe_step(e, entry->a, entry->b);
        break;
    case EVLOG_CHANGE_FLOOR:            /* 0x0b/0xb: g_battle_turns = 0 */
        e->aux.v[EVAUX_BATTLE_TURNS] = 0;
        /* mhyouji redraws the floor without the hero; only the floor-50
         * branch's yusyahyouji1 draws him again, at the arrival cell.
         * Timer1 would redraw him between steps, so the traversal hides
         * him until that floor-50 step. */
        e->floor_traversal = entry->a != 50;
        e->hero_erased = e->floor_traversal;
        break;
    case EVLOG_FADE:                    /* 0x0b/0xf (0x4539be): the form's
                                           canvas copied into 0x48c528 */
        e->screen_captures++;
        break;
    case EVLOG_HIDE_IMAGES:             /* 0x0b/0xe */
        memset(&e->cutscene, 0, sizeof(e->cutscene));
        e->hud_hidden = true;
        break;
    case EVLOG_DOOR_OPEN:               /* a=index b=kind c=tile d=progress */
        if (entry->c >= 0)
            set_door(e, entry->a, entry->b, entry->d);
        break;
    case EVLOG_MONSTER_MOVE:            /* a=from b=to c=raw */
        drop_actor(e, current_floor(e), entry->a);
        drop_actor(e, current_floor(e), entry->b);
        start_move(e, entry->a, entry->b, entry->c);
        break;
    case EVLOG_SET_TILE:                /* a=floor b=index c=tile d=silent */
        if (!entry->d) {
            drop_actor(e, entry->a, entry->b);
            int col = entry->b % MAP_TILE_COLS, row = entry->b / MAP_TILE_COLS;
            if (e->vampire.active && e->vampire.floor == entry->a &&
                col >= 4 && col <= 6 && row >= 4 && row <= 6)
                e->vampire.active = false;
        }
        break;
    case EVLOG_MONSTER_HP:              /* op 0x05 / 0x0e: Memo5 */
        e->monster_panel.life = entry->a;
        break;
    case EVLOG_VAMPIRE:                 /* a=stage */
        e->vampire = (EventVampire){ true, current_floor(e), entry->a };
        break;
    case EVLOG_DEATH_WINDOW:            /* the VM appended the timeline */
        /* op 0x03 starts by redrawing the player's cell in both buffers
         * with the map tile, before the message window: the hero vanishes
         * and the monster under him shows, like a perished monster. */
        e->battle_stage = false;
        drop_actor_at_player(e);
        e->hero_erased = true;
        e->aux.v[EVAUX_SUBEVENT_ID] = 9;           /* g_subevent_id */
        e->aux.v[EVAUX_MESSAGE_SELECTOR] = 0x1e;  /* g_dialog_selector */
        e->timer2 = true;                         /* zyouout: timer2on */
        break;
    case EVLOG_RESET:                   /* op 0x16 -> gameover 0x454da0 */
        game_over_reset(e);
        break;
    case EVLOG_PLAYER_UPDATE:           /* 0x0b/8, 0x0b/9: player redrawn */
        drop_actor_at_player(e);
        break;
    case EVLOG_PLAYER_ANIMATION:        /* 0x0b/9: yusyahyouji1 */
        e->battle_stage = false;        /* the exchange is over */
        drop_actor_at_player(e);
        hero_walk(e);
        break;
    default:
        break;
    }
}

/* Builders for the 0x0b splice sub-handlers (12-event-vm.md section 5). */
static const char *splice_builder(int sub)
{
    switch (sub) {
    case 1: return "openning31";        /* 0x4653b8 */
    case 2: return "zenolivework";      /* 0x461a0c */
    case 3: return "madoushi2";         /* 0x462560 */
    case 5: return "moncheck";          /* 0x4664e4 */
    case 10: return "taisen2";          /* 0x47f7d4 */
    default: return NULL;
    }
}

static void on_vm_sub(EventVm *vm, int kind, int sub, int arg2, void *user)
{
    (void)vm;
    (void)arg2;
    EventEngine *e = user;
    if (kind == EVOP_START_SUBEVENT) {
        /* op 0x0a (0x45317c..0x4531a8): g_subevent_id = arg1 + 10,
         * 0x48c5c8 = arg1, Timer2 off (0x453194), zyouout. */
        e->aux.v[EVAUX_SUBEVENT_ID] = sub + 10;
        /* word write (0x4531c9); the high word is itemlive's flag 0x48c5ca */
        e->aux.v[EVAUX_UI_SELECTOR] =
            (int32_t)(((uint32_t)e->aux.v[EVAUX_UI_SELECTOR] & 0xffff0000u) |
                      ((uint32_t)sub & 0xffffu));
        e->timer2 = false;
        zyouout(e);
        return;
    }
    if (sub == 6) {
        /* 0x4533fb: the VM set option A (0x2b) or B (1); gameover. */
        game_over_reset(e);
        return;
    }
    /* 0x0b splice: the count is already decremented; the sub-handler's
     * builder appends over this command.  An unported one is logged and
     * skipped (counted in e->unported). */
    if (sub == 10) {
        /* taisen2 reads the 0xb command's arg2 (target map index). */
        e->aux.v[EVAUX_TARGET_INDEX] = (int32_t)(arg2 & 0xffff);
    }
    if (sub == 2) {
        /* 0x4531da case 2: the tile at 0x1b is Zeno's cell.  The original
         * also sets DAT_0048c558 (work_558) to 0x1b before zenolivework;
         * the earlier doorstackin left the door's index there. */
        e->aux.v[EVAUX_WORK_558] = 0x1b;
    }
    const char *name = splice_builder(sub);
    if (name && e->have_builders && event_builders_has(&e->builders, name)) {
        if (event_builders_run(&e->builders, name, &e->vm, &e->aux))
            return;
        engine_log(e, "splice builder failed: %s", e->builders.err);
    }
    e->unported++;
    char what[32];
    snprintf(what, sizeof(what), "%d", sub);
    engine_log(e, "0x0b sub-event %s is not ported", what);
}

/* yousei's RichEdit1 text (0x47ae4a..0x47bab0): rank and score lines, then
 * a comment chosen by the score band (and by the visit count). */
static void append_text(EventEngine *e, const char *s)
{
    size_t len = strlen(e->dialog_text);
    snprintf(e->dialog_text + len, sizeof(e->dialog_text) - len, "%s", s);
}

static void append_int(EventEngine *e, int v)
{
    char buf[64];
    const char *g = joiner(e, false);
    snprintf(buf, sizeof(buf), "%s%d%s", g, v, g);     /* " %d " */
    append_text(e, buf);
}

static void fairy_text(EventEngine *e, int rank, int score, int visits)
{
    e->dialog_text[0] = '\0';
    if (rank >= 1) {
        append_message(e, 0xf4);
        append_int(e, rank);
        append_message(e, 0xf5);
    }
    append_message(e, 0xdc);
    append_int(e, score);
    append_message(e, 0xdd);
    unsigned u = (unsigned)score;
    int band = -1;                       /* the middle message */
    if (score < 100) {
        if (score < 50) { append_message(e, 0xde); return; }
        if (u - 50 < 10) { append_message(e, 0xdf); return; }
        if (u - 60 < 20) { append_message(e, 0xe0); return; }
        if (u - 80 < 20) { append_message(e, 0xe1); return; }
        return;
    }
    if (u - 100 < 21)        band = 0xe5;
    else if (u - 121 < 20)   band = 0xe6;
    else if (u - 141 < 20)   band = 0xe7;
    else if (u - 161 < 40)   band = 0xe8;
    else if (u - 201 < 0x2647) band = 0xe9;
    if (band < 0)
        return;
    append_message(e, band <= 0xe6 ? 0xe2 : 0xe3);
    append_message(e, 0xe4);
    append_message(e, band);
    append_message(e, 0xea);
    append_message(e, visits < 4 ? 0xeb : 0xec);
}

/* Button2Click, choice_mode 1 (0x44e6b0..): RichEdit1 = ListBox2[0xcc];
 * Button39 = [0xcd] life [0xce], Button40 = [0xcf] attack [0xd0],
 * Button41 = [0xd1] defense [0xd0], Button42 = [0xd2]; focus Button42. */
static void option_text(EventEngine *e, char *out, size_t outsz, int a,
                        int n, int b)
{
    const char *m1 = e->have_messages ? messages_get(&e->messages, a) : NULL;
    const char *m2 = b >= 0 && e->have_messages ? messages_get(&e->messages, b)
                                                : NULL;
    if (b < 0)
        snprintf(out, outsz, "%s", m1 ? m1 : "[message]");
    else
        snprintf(out, outsz, "%s%s%d%s%s", m1 ? m1 : "", joiner(e, false),
                 n, joiner(e, false), m2 ? m2 : "");    /* "%s %d %s" */
}

static void altar_menu(EventEngine *e, int life, int attack, int defense)
{
    e->dialog_text[0] = '\0';
    append_message(e, 0xcc);
    e->dialog_visible = true;
    option_text(e, e->altar_options[0], sizeof(e->altar_options[0]),
                0xcd, life, 0xce);
    option_text(e, e->altar_options[1], sizeof(e->altar_options[1]),
                0xcf, attack, 0xd0);
    option_text(e, e->altar_options[2], sizeof(e->altar_options[2]),
                0xd1, defense, 0xd0);
    option_text(e, e->altar_options[3], sizeof(e->altar_options[3]),
                0xd2, 0, -1);
    e->altar_open = true;
    e->altar_focus = 3;
}

/* Status-bar text from parts (Button38Click's LStrCatN/LStrCat3):
 * (0, id) ListBox2[id], (1, n) the number n, (2, 0) the " " literal. */
static void status_cat(EventEngine *e, const int32_t *args, int nargs)
{
    char *out = e->status_text;
    size_t size = sizeof(e->status_text), len = 0;
    out[0] = '\0';
    for (int i = 0; i + 1 < nargs && len < size; i += 2) {
        int tag = args[i];
        int v = args[i + 1];
        const char *m = NULL;
        char num[16];
        if (tag == 0) {
            m = e->have_messages ? messages_get(&e->messages, v) : NULL;
            if (!m) {
                snprintf(num, sizeof(num), "[%d]", v);
                m = num;
            }
        } else if (tag == 1) {
            snprintf(num, sizeof(num), "%d", v);
            m = num;
        } else {
            m = joiner(e, v == 1);      /* code literals ". " / " " */
        }
        int n = snprintf(out + len, size - len, "%s", m);
        if (n < 0)
            break;
        len += (size_t)n;
    }
    e->status_fresh = true;
}

void event_engine_disp(EventEngine *e)
{
    char text[EVENT_ENGINE_TEXT_LEN];
    int sprite = e->aux.v[EVAUX_SPRITE];
    if (sprite == 1) {
        snprintf(text, sizeof(text), "%s", e->status_text);
    } else {
        const char *m = e->have_messages
            ? messages_get(&e->messages, sprite + 0x7f) : NULL;
        snprintf(text, sizeof(text), "%s", m ? m : "");
    }
    /* The console keeps what Edit8 showed: a text once per action (a
     * redraw with the same text adds nothing). */
    bool changed = strcmp(text, e->status_bar) != 0;
    bool fresh = e->status_fresh || e->builders.sprite_set;
    snprintf(e->status_bar, sizeof(e->status_bar), "%s", text);
    if (text[0] && (changed || fresh))
        engine_log(e, "%s", text);
    e->status_fresh = false;
    e->builders.sprite_set = false;
}

static void show_message(EventEngine *e, int ui_entry)
{
    const char *m = e->have_ui_texts ? messages_get(&e->ui_texts, ui_entry)
                                     : NULL;
    if (m)
        snprintf(e->message_text, sizeof(e->message_text), "%s", m);
    else
        snprintf(e->message_text, sizeof(e->message_text),
                 "[message %d]", ui_entry);
    e->message_open = true;
    engine_log(e, "%s", e->message_text);
}

/* Form2's FormShow (FUN_00436c14) for the fairy's area. */
static void ranking_show(EventEngine *e)
{
    const StateBlock *s = e->vm.state;
    e->ranking_open = true;
    e->ranking_area = s ? s->raw[0x57] : 0;
    e->ranking_rank = s ? s->raw[0x56] : 0;
    e->ranking_score = s ? s->raw[0x55] : 0;
    e->ranking_name[0] = '\0';
    int32_t magic = s ? s->raw[0x9d] : -4;
    int status = RANKING_MISSING;
    if (e->ranking_area >= 1 && e->ranking_area <= 5 && e->ranking_dir[0])
        status = ranking_load(&e->ranking, e->ranking_dir, e->ranking_area,
                              magic);
    else
        ranking_builtin(&e->ranking, e->ranking_area);
    /* FUN_00437f2c's own ShowMessage for a broken file; an unreadable
     * one leaves the temporary ranking (the remake's first run). */
    if (status == RANKING_BAD_SUMS || status == RANKING_BAD_VERSION)
        show_message(e, 2 + status);
    e->ranking_name_entry = e->ranking_rank > 0;
    e->ranking_view_area = e->ranking_area;
}

bool event_engine_show_ranking(EventEngine *e, int area)
{
    if (event_engine_active(e) || area < 1 || area > 5)
        return false;
    StateBlock *s = e->vm.state;
    if (s) {
        s->raw[0x56] = 0;
        s->raw[0x57] = 0;
    }
    e->ranking_open = true;
    e->ranking_area = area;
    e->ranking_view_area = area;
    e->ranking_rank = 0;
    e->ranking_score = 0;
    e->ranking_name_entry = false;
    int32_t magic = s ? s->raw[0x9d] : -4;
    int status = RANKING_MISSING;
    if (e->ranking_dir[0])
        status = ranking_load(&e->ranking, e->ranking_dir, area, magic);
    else
        ranking_builtin(&e->ranking, area);
    if (status == RANKING_BAD_SUMS || status == RANKING_BAD_VERSION)
        show_message(e, 2 + status);
    return true;
}

bool event_engine_ranking_browse(EventEngine *e, int delta)
{
    if (!e->ranking_open || e->ranking_name_entry)
        return false;
    int area = e->ranking_view_area;
    if (area < 1 || area > 5)
        area = 1;
    area = (area - 1 + delta % 5 + 5) % 5 + 1;
    e->ranking_view_area = area;
    if (area != e->ranking_area) {
        int32_t magic = e->vm.state ? e->vm.state->raw[0x9d] : -4;
        if (e->ranking_dir[0])
            ranking_load(&e->ranking_view, e->ranking_dir, area, magic);
        else
            ranking_builtin(&e->ranking_view, area);
    }
    return true;
}

const Ranking *event_engine_ranking_shown(const EventEngine *e)
{
    return e->ranking_view_area >= 1 && e->ranking_view_area <= 5 &&
           e->ranking_view_area != e->ranking_area ? &e->ranking_view
                                                   : &e->ranking;
}

static void on_present(void *user, const char *kind, const int32_t *args,
                       int nargs)
{
    EventEngine *e = user;
    if (strcmp(kind, "timer2on") == 0) {
        e->timer2 = true;                      /* timer2on 0x44bc0c */
    } else if (strcmp(kind, "timer2") == 0) {
        e->timer2 = nargs > 0 && args[0] != 0; /* TTimer.SetEnabled */
    } else if (strcmp(kind, "zyouout") == 0) {
        zyouout(e);
    } else if (strcmp(kind, "player_step") == 0) {
        /* idou moves the player with yusyaidou; the caller animates the
         * state x/y delta instead of showing an instant jump. */
        e->player_step_pending = true;
        if (nargs > 0)                  /* the destination is redrawn */
            drop_actor(e, current_floor(e), (int)args[0]);
    } else if (strcmp(kind, "media_stop") == 0) {
        audio_push(e, EVENT_AUDIO_BGM_STOP, 0);  /* MediaPlayer5.Stop */
    } else if (strcmp(kind, "bgm") == 0 && nargs > 0) {
        if (args[0] != 0)                        /* soundplay */
            audio_push(e, EVENT_AUDIO_BGM, args[0]);
    } else if (strcmp(kind, "vampire_block") == 0) {
        /* Stage 0 of banpaiawork's block is what the map buffers still
         * show: the bats on their cells, the vampire's mask all white. */
        e->vampire = (EventVampire){ true, current_floor(e), 0 };
    } else if (strcmp(kind, "mhyouji") == 0) {
        e->hero_erased = true;          /* the floor's tiles, no hero */
    } else if (strcmp(kind, "player_draw") == 0) {
        drop_actor_at_player(e);        /* yusyahyouji1 */
        hero_walk(e);
    } else if (strcmp(kind, "timer1") == 0) {
        e->timer1 = nargs > 0 && args[0] != 0;  /* TTimer.SetEnabled */
    } else if (strcmp(kind, "fade_hide") == 0 && nargs >= 4) {
        /* A masked fade-in writes its sprite tile before the first frame.
         * The original's buffers are updated only by the composite, but the
         * remake draws the map from its data, so cover the cell with the
         * first (white) mask now; the first MASKED_TILE then updates the
         * same actor and the fade runs. */
        set_actor(e, (int)args[0], (int)args[1], (int)args[2], (int)args[3]);
    } else if (strcmp(kind, "battle_stage") == 0 && nargs >= 1) {
        /* taisen / taisen2: stage the one-square battle.  args: target
         * tile, first target (0 monster, 1 hero). */
        e->battle_stage = true;
        e->battle_target = args[0];
        e->battle_first_target = nargs > 1 && args[1] != 0;
        e->battle_ticks = 0;
    } else if (strcmp(kind, "status_text") == 0 && nargs > 0) {
        const char *m = e->have_messages ? messages_get(&e->messages, args[0])
                                         : NULL;
        if (m)
            snprintf(e->status_text, sizeof(e->status_text), "%s", m);
        else
            snprintf(e->status_text, sizeof(e->status_text), "[message %d]",
                     (int)args[0]);
        e->status_fresh = true;
    } else if (strcmp(kind, "richedit_text") == 0 && nargs > 0) {
        e->dialog_text[0] = '\0';     /* RichEdit1.Text := ListBox2[id] */
        if (args[0] >= 0)
            append_message(e, args[0]);
    } else if (strcmp(kind, "richedit_show") == 0 && nargs > 0) {
        e->dialog_visible = args[0] != 0;
    } else if (strcmp(kind, "button_caption") == 0 ||
               strcmp(kind, "button_caption_clear") == 0) {
        set_button_caption(e, nargs > 0 ? (int)args[0] : -1);
    } else if (strcmp(kind, "button1_show") == 0) {
        e->dialog_open = true;                 /* Button1 visible, focused */
        e->dialog_choice = false;
        e->dialog_id = e->aux.v[EVAUX_SUBEVENT_ID];
    } else if (strcmp(kind, "choice_show") == 0) {
        e->dialog_open = true;                 /* Button2 + Button7 */
        e->dialog_choice = true;
        e->choice_no_enabled = nargs > 0 && args[0] != 0;
        /* The original focuses Button2 (Yes); the remake starts on the
         * no-action answer when there is one. */
        e->choice_focus_no = e->choice_no_enabled;
        e->dialog_id = e->aux.v[EVAUX_SUBEVENT_ID];
    } else if (strcmp(kind, "choice_hide") == 0) {
        e->dialog_choice = false;
        e->choice_no_enabled = false;
    } else if (strcmp(kind, "altar_offer") == 0 && nargs > 0) {
        e->dialog_text[0] = '\0';     /* ListBox2[0xca] price [0xcb] */
        append_message(e, 0xca);
        append_int(e, args[0]);
        append_message(e, 0xcb);
    } else if (strcmp(kind, "altar_menu") == 0 && nargs >= 3) {
        altar_menu(e, args[0], args[1], args[2]);
    } else if (strcmp(kind, "altar_menu_hide") == 0) {
        e->altar_open = false;
    } else if (strcmp(kind, "status_concat") == 0 && nargs >= 3) {
        /* LStrCatN(0x4b8684): ListBox2[a] " " n ListBox2[b] */
        const char *m1 = e->have_messages ? messages_get(&e->messages, args[0]) : NULL;
        /* A negative b is a code literal: ui.tsv entry -b. */
        const char *m2 = args[2] < 0
            ? (e->have_ui_texts ? messages_get(&e->ui_texts, -args[2]) : NULL)
            : (e->have_messages ? messages_get(&e->messages, args[2]) : NULL);
        snprintf(e->status_text, sizeof(e->status_text), "%s%s%d%s",
                 m1 ? m1 : "", joiner(e, false), (int)args[1],
                 m2 ? m2 : "");                     /* "%s %d%s" */
        e->status_fresh = true;
    } else if (strcmp(kind, "timer2_stairs") == 0) {
        /* kaidanwork: Timer2.Interval 2/6/10 ms by the speed menu. */
        e->vm.interval_ms = event_vm_stairs_interval_ms(e->vm.speed);
    } else if (strcmp(kind, "checkpoint") == 0) {
        e->checkpoint_pending = true;
    } else if (strcmp(kind, "wipe_begin") == 0 && nargs >= 3) {
        /* kaidanwork: the screen still shows the floor being left. */
        memset(&e->wipe, 0, sizeof(e->wipe));
        e->wipe.active = true;
        e->wipe.old_floor = args[0];
        e->wipe.old_player = args[1];
        e->wipe.old_facing = args[2];
        if (e->vm.map && args[0] >= 0 && args[0] < MAP_FLOOR_COUNT)
            for (int i = 0; i < 121; i++)
                e->wipe.old_tiles[i] = map_tile(e->vm.map, args[0], i);
    } else if (strcmp(kind, "monshyouji") == 0 && nargs >= 2) {
        monshyouji(e, (int)args[1]);
    } else if (strcmp(kind, "disp") == 0) {
        event_engine_disp(e);
    } else if (strcmp(kind, "ranking_show") == 0) {
        ranking_show(e);                       /* Form2.ShowModal */
    } else if (strcmp(kind, "ranking_message") == 0 && nargs > 0) {
        /* yousei's ShowMessage: ui.tsv 3 (sums), 4 (version).  An
         * unreadable file (5) is the remake's first run -- no ranking
         * files are shipped -- so it only goes to the log. */
        if (args[0] == RANKING_MISSING)
            engine_log(e, "ranking: %s", "no ranking file yet");
        else
            show_message(e, 2 + args[0]);
    } else if (strcmp(kind, "fairy_text") == 0 && nargs >= 3) {
        fairy_text(e, args[0], args[1], args[2]);
    } else if (strcmp(kind, "dialog_hide") == 0) {
        e->dialog_visible = false;             /* RichEdit1.Visible := False */
    } else if (strcmp(kind, "status_cat") == 0) {
        status_cat(e, args, nargs);
    } else if (strcmp(kind, "item_confirm") == 0 && nargs > 0) {
        e->item_confirm = true;                /* Panel form+0x2cc/0x2d0 */
        e->item_confirm_case = args[0];
        e->item_confirm_focus_no = true;       /* No first */
    } else if (strcmp(kind, "item_confirm_hide") == 0) {
        e->item_confirm = false;
    } else if (strcmp(kind, "fly_panel") == 0 && nargs >= 2) {
        e->fly_open = true;                    /* Button8/9/10 */
        e->fly_up_enabled = args[0] != 0;
        e->fly_down_enabled = args[1] != 0;
    } else if (strcmp(kind, "fly_panel_hide") == 0) {
        e->fly_open = false;
    } else if (strcmp(kind, "ending_hide") == 0) {
        e->ending_wait = false;               /* Panel2/Image34 hidden */
    } else if (strcmp(kind, "hero_row") == 0 && nargs >= 2) {
        if (e->hero_rows < 7) {
            e->hero_row_index[e->hero_rows] = args[0];
            e->hero_row_damage[e->hero_rows] = args[1];
            e->hero_rows++;
        }
    } else if (strcmp(kind, "hero_orb") == 0) {
        e->hero_orb_pending = true;
    } else if (strcmp(kind, "wisdom_button") == 0 && nargs >= 2) {
        if (args[0] >= 0 && args[0] < 24)
            e->wisdom_enabled[args[0]] = args[1] != 0;
    } else if (strcmp(kind, "wisdom_orb") == 0) {
        e->wisdom_orb_pending = true;
        e->wisdom_open = true;
        e->wisdom_focus = -1;
        for (int k = 0; k < 24; k++)
            if (e->wisdom_enabled[k]) {
                e->wisdom_focus = k;
                break;
            }
    } else if (strcmp(kind, "wisdom_close") == 0) {
        e->wisdom_open = false;
    }
}

/* --------------------------------------------------------------------- */

/* yousei's BlockRead of optionN.dat from the save directory. */
static bool read_ranking_file(void *user, int area, int32_t out[72])
{
    EventEngine *e = user;
    if (!e->ranking_dir[0] || area < 1 || area > 5)
        return false;
    char p[1100];
    snprintf(p, sizeof(p), "%s/option%d.dat", e->ranking_dir, area);
    size_t n = 0;
    char *data = file_load(p, &n);
    if (!data)
        return false;
    const uint8_t *raw = (const uint8_t *)data;
    if (n > 288)
        n = 288;
    memset(out, 0, 72 * sizeof(int32_t));
    for (size_t i = 0; i + 3 < n; i += 4)          /* zeros past the end */
        out[i / 4] = (int32_t)((uint32_t)raw[i] | (uint32_t)raw[i + 1] << 8 |
                               (uint32_t)raw[i + 2] << 16 |
                               (uint32_t)raw[i + 3] << 24);
    file_free(data);
    return true;
}

bool event_engine_init(EventEngine *e, const char *builders_path,
                       char *err, size_t errsz)
{
    EventEngineLogFn log_fn = e->log_fn;
    void *log_user = e->log_user;
    memset(e, 0, sizeof(*e));
    e->log_fn = log_fn;
    e->log_user = log_user;
    event_vm_init(&e->vm, NULL, NULL);
    e->vm.observer = on_vm_log;
    e->vm.observer_user = e;
    e->vm.sub_fn = on_vm_sub;
    e->vm.sub_user = e;
    e->aux.v[EVAUX_DIRECTION] = 1;
    e->monster_panel.index = -1;
    timer1_reset(e);
    set_button_caption(e, -1);
    if (!builders_path || !*builders_path) {
        if (err && errsz) snprintf(err, errsz, "no builder script");
        return false;
    }
    e->have_builders = event_builders_init(&e->builders, builders_path,
                                           err, errsz);
    e->builders.present_fn = on_present;
    e->builders.present_user = e;
    e->builders.ranking_fn = read_ranking_file;
    e->builders.ranking_user = e;
    return e->have_builders;
}

bool event_engine_load_messages(EventEngine *e, const char *path,
                                char *err, size_t errsz)
{
    if (e->have_messages)
        messages_free(&e->messages);
    e->have_messages = messages_load(&e->messages, path, err, errsz);
    if (!e->speaker_names.name[0][0])
        speaker_names_init(&e->speaker_names);
    return e->have_messages;
}

bool event_engine_load_speakers(EventEngine *e, const char *path)
{
    speaker_names_init(&e->speaker_names);
    return speaker_names_load(&e->speaker_names, path);
}

void event_engine_shutdown(EventEngine *e)
{
    if (e->have_ui_texts)
        messages_free(&e->ui_texts);
    e->have_ui_texts = false;
    if (e->have_builders)
        event_builders_shutdown(&e->builders);
    e->have_builders = false;
    if (e->have_messages)
        messages_free(&e->messages);
    e->have_messages = false;
}

void event_engine_bind(EventEngine *e, StateBlock *state, MapBlock *map)
{
    e->vm.state = state;
    e->vm.map = map;
    e->battle_stage = false;
    e->floor_traversal = false;
    e->ending_verse_wait = false;
    e->ending_verse_at = 0;
    e->ending_button_at = 0;
    /* syokidata2/timerin put Timer2 back on the speed-menu interval. */
    e->vm.interval_ms = event_vm_speed_interval_ms(e->vm.speed);
    event_vm_syokidata2_zeno(&e->vm);   /* load / new game */
}

const char *event_engine_route(int floor, int index, uint8_t raw)
{
    /* formkeydown (0x460bd8) sends every move to handan (0x444490), which
     * is ported for every raw value (16-event-pilot.md section 12c). */
    (void)floor;
    (void)index;
    (void)raw;
    return "handan";
}

/* Engine-side state that builders change through present() and that the
 * builder runtime's fail-closed restore does not cover. */
typedef struct {
    bool timer2, dialog_visible, dialog_open, dialog_choice, choice_no_enabled;
    int dialog_id, dialog_speaker;
    char dialog_text[EVENT_ENGINE_TEXT_LEN];
    char button_caption[64];
    EventMonsterPanel monster_panel;
} EngineSaved;

static void save_engine(const EventEngine *e, EngineSaved *s)
{
    s->timer2 = e->timer2;
    s->dialog_visible = e->dialog_visible;
    s->dialog_open = e->dialog_open;
    s->dialog_choice = e->dialog_choice;
    s->choice_no_enabled = e->choice_no_enabled;
    s->dialog_id = e->dialog_id;
    s->dialog_speaker = e->dialog_speaker;
    memcpy(s->dialog_text, e->dialog_text, sizeof(s->dialog_text));
    memcpy(s->button_caption, e->button_caption, sizeof(s->button_caption));
    s->monster_panel = e->monster_panel;
}

static void restore_engine(EventEngine *e, const EngineSaved *s)
{
    e->timer2 = s->timer2;
    e->dialog_visible = s->dialog_visible;
    e->dialog_open = s->dialog_open;
    e->dialog_choice = s->dialog_choice;
    e->choice_no_enabled = s->choice_no_enabled;
    e->dialog_id = s->dialog_id;
    e->dialog_speaker = s->dialog_speaker;
    memcpy(e->dialog_text, s->dialog_text, sizeof(s->dialog_text));
    memcpy(e->button_caption, s->button_caption, sizeof(s->button_caption));
    e->monster_panel = s->monster_panel;
}

EventEngineResult event_engine_trigger(EventEngine *e, int floor, int index,
                                       uint8_t raw, int direction)
{
    const char *builder = event_engine_route(floor, index, raw);
    if (!builder || !e->have_builders || !e->vm.state || !e->vm.map)
        return EVENT_ENGINE_NOT_ROUTED;
    if (event_engine_active(e))
        return EVENT_ENGINE_FAILED;
    /* formkeydown: a timeline count left at -1 by gameover reads as 0. */
    if (e->vm.count < 0)
        e->vm.count = 0;
    EngineSaved saved;
    save_engine(e, &saved);
    e->aux.v[EVAUX_TARGET_INDEX] = index;
    e->aux.v[EVAUX_DIRECTION] = direction;
    e->player_step_pending = false;
    if (!event_builders_run(&e->builders, builder, &e->vm, &e->aux)) {
        restore_engine(e, &saved);
        engine_log(e, "event builder failed: %s", e->builders.err);
        return EVENT_ENGINE_FAILED;
    }
    e->ticking = false;
    return EVENT_ENGINE_STARTED;
}

void event_engine_timer1(EventEngine *e)
{
    /* Timer1Timer (0x443120), outside battle rendering: standing where the
     * floor-15 Giant Octopus or the floor-35 Dragon fell (cell 60 / 71,
     * now floor) runs takowork1/2, which repaint the big monster's
     * leftover cells on the back buffer (presentation: the remake draws
     * from the map), and counts the visit in state[0x98] / [0x99], at most
     * twice. */
    StateBlock *s = e->vm.state;
    MapBlock *m = e->vm.map;
    if (!e->timer1)
        return;
    /* Show the buffer 0x48c5d2 names, then (outside battle rendering)
     * redraw the hero standing in it; toggle 0x48c5d2. */
    int shown = e->timer1_next;
    e->shown_buffer = shown;
    e->timer1_next ^= 1;
    /* The blit: what that buffer holds now is what the form shows. */
    e->hero_shown_pose = e->hero_pose[shown];
    for (int i = 0; i < EVENT_ENGINE_MAX_ACTORS; i++)
        e->actors[i].on_screen = (e->actors[i].buffers >> shown) & 1u;
    if (!s || !m || s->raw[0x0c] != 0)   /* battle_render_flag */
        return;
    e->hero_erased = false;
    e->hero_pose[shown] = EVENT_HERO_STAND;
    drop_actor_at_player_buffer(e, shown);
    int floor = s->raw[ST_FLOOR];
    int p = s->raw[ST_Y] * MAP_TILE_COLS + s->raw[ST_X];
    if (floor == 15 && p == 0x3c && map_tile(m, 15, 0x3c) == 6 &&
        s->raw[0x98] < 2)
        s->raw[0x98]++;
    if (floor == 35 && p == 0x47 && map_tile(m, 35, 0x47) == 6 &&
        s->raw[0x99] < 2)
        s->raw[0x99]++;
}

bool event_engine_active(const EventEngine *e)
{
    /* A timeline left with Timer2 off and nothing to answer waits for the
     * next timer2on without locking the player. */
    return (e->vm.count > 0 && (e->timer2 || e->ticking)) || e->dialog_open ||
           e->ranking_open || e->altar_open || e->death_prompt ||
           e->item_confirm || e->fly_open || e->ending_wait ||
           e->ending_verse_wait || e->message_open || e->wisdom_open;
}

bool event_engine_close_message(EventEngine *e)
{
    if (!e->message_open)
        return false;
    e->message_open = false;
    return true;
}

bool event_engine_ranking_name(EventEngine *e, const char *name, bool ok)
{
    if (!e->ranking_open || !e->ranking_name_entry)
        return false;
    e->ranking_name_entry = false;
    const StateBlock *s = e->vm.state;
    int rank = e->ranking_rank;
    if (ok) {
        if (!name || !*name)
            rank = 0;                   /* empty name: nothing inserted */
        if (rank > 0 && s)
            ranking_insert(&e->ranking, rank, e->ranking_score,
                           s->raw[0], s->raw[1], s->raw[2], name);
        snprintf(e->ranking_name, sizeof(e->ranking_name), "%s",
                 name ? name : "");
    }
    /* FUN_00439ce0 runs when the rank is still positive (also after
     * Cancel, which writes the table back unchanged). */
    if (rank > 0) {
        int32_t magic = s ? s->raw[0x9d] : -4;
        ranking_seal(&e->ranking, e->ranking_area, magic);
        char err[256] = "";
        if (e->ranking_dir[0] &&
            !ranking_save(&e->ranking, e->ranking_dir, e->ranking_area,
                          err, sizeof(err)))
            engine_log(e, "ranking: %s", err);
        e->ranking_checkpoint_pending = true;
    }
    e->ranking_rank = rank;
    return true;
}

bool event_engine_close_ranking(EventEngine *e)
{
    if (!e->ranking_open || e->ranking_name_entry)
        return false;
    e->ranking_open = false;
    /* TForm2.form2onclose (0x43a504) re-enables the main form's Timer2, so
     * the timeline that Button1Click 0x1a left stopped (yousei: the gate and
     * fairy tiles) resumes once the ranking window closes. */
    e->timer2 = true;
    return true;
}

const char *event_engine_dialog(const EventEngine *e)
{
    return e->dialog_visible ? e->dialog_text : NULL;
}

const char *event_engine_button_caption(const EventEngine *e)
{
    return e->button_caption;
}

int event_engine_dialog_speaker(const EventEngine *e)
{
    if (!e->dialog_visible || !e->dialog_text[0] || e->dialog_speaker <= 0)
        return -1;
    return e->dialog_speaker - 1;
}

const char *event_engine_speaker_name(const EventEngine *e, int speaker)
{
    const char *n = speaker_name(&e->speaker_names, speaker);
    if (n && n[0])
        return n;
    const SpeakerDef *d = speaker_def(speaker);
    return d ? d->name : NULL;
}

bool event_engine_choose(EventEngine *e, bool yes)
{
    if (!e->dialog_open || !e->dialog_choice)
        return false;
    if (!yes && !e->choice_no_enabled)
        return false;
    const char *name = yes ? "button2click" : "button7click";
    EngineSaved saved;
    save_engine(e, &saved);
    e->dialog_open = false;
    if (!e->have_builders || !e->vm.state ||
        !event_builders_run(&e->builders, name, &e->vm, &e->aux)) {
        restore_engine(e, &saved);
        e->dialog_open = false;
        e->dialog_choice = false;
        e->dialog_visible = false;
        engine_log(e, "choice not ported: %s", e->builders.err);
    }
    e->ticking = false;
    return true;
}

bool event_engine_death_answer(EventEngine *e, bool yes)
{
    if (!e->death_prompt)
        return false;
    e->death_prompt = false;
    if (yes)
        e->timer2 = true;                  /* timer2on 0x44bc0c */
    else
        e->load_enabled = true;            /* GameLoad1.SetEnabled(True) */
    e->ticking = false;
    return true;
}

void event_engine_set_initial_map(EventEngine *e, const MapBlock *map)
{
    e->initial_map = map;
}

bool event_engine_altar_choose(EventEngine *e, int option)
{
    static const char *const names[4] = {
        "button39click", "button40click", "button41click", "button42click"
    };
    if (!e->altar_open || option < 0 || option > 3)
        return false;
    EngineSaved saved;
    save_engine(e, &saved);
    e->altar_open = false;
    if (!e->have_builders || !e->vm.state ||
        !event_builders_run(&e->builders, names[option], &e->vm, &e->aux)) {
        restore_engine(e, &saved);
        e->altar_open = false;
        e->dialog_visible = false;
        engine_log(e, "altar option not ported: %s", e->builders.err);
    }
    e->ticking = false;
    return true;
}

bool event_engine_confirm(EventEngine *e)
{
    if (!e->dialog_open || e->dialog_choice)
        return false;
    EngineSaved saved;
    save_engine(e, &saved);
    e->dialog_open = false;                /* Button1 hidden */
    if (!e->have_builders || !e->vm.state ||
        !event_builders_run(&e->builders, "button1click", &e->vm, &e->aux)) {
        restore_engine(e, &saved);
        e->dialog_open = false;
        e->dialog_visible = false;
        engine_log(e, "button1click not ported: %s", e->builders.err);
        e->unported++;
    }
    e->ticking = false;
    return true;
}

/* formkeydown Enter on the item overlay: the handler of `slot`. */
static const char *const ITEM_HANDLERS[15] = {
    "img6work", "img5work", "img4work", NULL, "img8work", "img9work",
    "img10work", "img11work", "img12work", "img13work", NULL,
    "img15work", "img16work", "img17work", NULL,
};

static EventEngineResult run_named(EventEngine *e, const char *name)
{
    if (!e->have_builders || !e->vm.state || !e->vm.map)
        return EVENT_ENGINE_NOT_ROUTED;
    if (e->vm.count < 0)
        e->vm.count = 0;
    EngineSaved saved;
    save_engine(e, &saved);
    bool confirm = e->item_confirm, fly = e->fly_open;
    if (!event_builders_run(&e->builders, name, &e->vm, &e->aux)) {
        restore_engine(e, &saved);
        e->item_confirm = confirm;
        e->fly_open = fly;
        engine_log(e, "event builder failed: %s", e->builders.err);
        return EVENT_ENGINE_FAILED;
    }
    e->ticking = false;
    return EVENT_ENGINE_STARTED;
}

const char *event_engine_item_handler(int slot)
{
    return (slot >= 0 && slot < 15) ? ITEM_HANDLERS[slot] : NULL;
}

EventEngineResult event_engine_use_item(EventEngine *e, int slot)
{
    const char *name = event_engine_item_handler(slot);
    if (!name)
        return EVENT_ENGINE_NOT_ROUTED;
    if (event_engine_active(e))
        return EVENT_ENGINE_FAILED;
    e->hero_rows = 0;
    return run_named(e, name);
}

bool event_engine_item_answer(EventEngine *e, bool yes)
{
    if (!e->item_confirm)
        return false;
    e->item_confirm = false;           /* both buttons hide the panel */
    run_named(e, yes ? "button38click" : "button37click");
    return true;
}

bool event_engine_fly(EventEngine *e, int action)
{
    if (!e->fly_open)
        return false;
    if (action > 0 && !e->fly_up_enabled)
        return false;
    if (action < 0 && !e->fly_down_enabled)
        return false;
    run_named(e, action > 0 ? "button8click"
                 : action < 0 ? "button9click" : "button10click");
    return true;
}

bool event_engine_wisdom_press(EventEngine *e, int k)
{
    if (!e->wisdom_open || k < 0 || k > 24)
        return false;
    if (k < 24 && !e->wisdom_enabled[k])
        return false;
    char name[32];
    snprintf(name, sizeof(name), "button%dclick", k < 24 ? 11 + k : 35);
    run_named(e, name);
    return true;
}

void event_engine_wisdom_move(EventEngine *e, int delta)
{
    if (!e->wisdom_open || !delta)
        return;
    /* The list holds the enabled buttons only; the words of the one
     * highlighted show at once (its Button11..34Click). */
    int k = e->wisdom_focus;
    for (int step = 0; step < 24; step++) {
        k = (k + (delta > 0 ? 1 : 23)) % 24;
        if (e->wisdom_enabled[k])
            break;
    }
    if (k >= 0 && k < 24 && e->wisdom_enabled[k] && k != e->wisdom_focus) {
        e->wisdom_focus = k;
        event_engine_wisdom_press(e, k);
    }
}

bool event_engine_ending_continue(EventEngine *e)
{
    if (!e->ending_wait)
        return false;
    if (run_named(e, "append") != EVENT_ENGINE_STARTED) {
        e->ending_wait = false;
        e->timer2 = true;
    }
    return true;
}

bool event_engine_ending_verse_continue(EventEngine *e)
{
    if (!e->ending_verse_wait)
        return false;
    e->ending_verse_wait = false;
    e->timer2 = true;
    return true;
}

bool event_engine_update(EventEngine *e, uint64_t now)
{
    if (!e->timer2 || e->vm.count <= 0) {
        bool drained = e->ticking && e->vm.count <= 0;
        e->ticking = false;
        if (e->vm.count <= 0)
            e->battle_stage = false;
        return drained;
    }
    if (!e->ticking) {
        e->ticking = true;
        e->next_tick = now + (uint64_t)e->vm.interval_ms;
        return false;
    }
    int guard = 0;
    while (e->timer2 && e->vm.count > 0 && now >= e->next_tick &&
           guard++ < 256) {
        /* timer2timer: two stackwork calls even if the first one disabled
         * Timer2 (op 0x0a); the next tick then does not come. */
        e->now = e->next_tick;
        event_vm_tick(&e->vm);
        if (e->battle_stage)
            e->battle_ticks++;
        int period = e->vm.interval_ms > 0 ? e->vm.interval_ms : 1;
        e->next_tick += (uint64_t)period;
        if (e->next_tick < e->hold_until)
            e->next_tick = e->hold_until;
    }
    if (e->vm.count <= 0) {
        e->ticking = false;
        e->battle_stage = false;
        return true;
    }
    if (!e->timer2)
        e->ticking = false;
    return false;
}

const EventVmActor *event_engine_actor_at(const EventEngine *e, int floor,
                                          int map_index)
{
    for (int i = 0; i < EVENT_ENGINE_MAX_ACTORS; i++) {
        const EventVmActor *a = &e->actors[i];
        if (a->active && a->floor == floor && a->map_index == map_index)
            return a;
    }
    return NULL;
}

void event_engine_clear_actors(EventEngine *e)
{
    memset(e->actors, 0, sizeof(e->actors));
    memset(e->doors, 0, sizeof(e->doors));
    memset(&e->vampire, 0, sizeof(e->vampire));
    /* Moves expire on their own (and are tied to a floor), so the last
     * step of a timeline still slides after the engine goes idle. */
}

void event_engine_cancel(EventEngine *e)
{
    event_vm_clear(&e->vm);
    e->vm.interval_ms = event_vm_speed_interval_ms(e->vm.speed);
    memset(&e->aux, 0, sizeof(e->aux));
    e->aux.v[EVAUX_DIRECTION] = 1;
    e->unported = 0;
    e->ticking = false;
    e->timer2 = false;
    e->battle_stage = false;
    monshyouji(e, -1);                  /* new game / load: syokidata2 */
    e->screen_captures = 0;
    timer1_reset(e);
    e->hud_hidden = false;
    e->dialog_visible = false;
    e->altar_open = false;
    e->ranking_open = false;
    e->ranking_name_entry = false;
    e->ranking_checkpoint_pending = false;
    e->message_open = false;
    e->checkpoint_pending = false;
    e->dialog_open = false;
    e->dialog_choice = false;
    e->choice_no_enabled = false;
    e->death_prompt = false;
    e->item_confirm = false;
    e->fly_open = false;
    e->ending_wait = false;
    e->hero_orb_pending = false;
    e->wisdom_orb_pending = false;
    e->wisdom_open = false;
    e->dialog_id = 0;
    e->dialog_text[0] = '\0';
    e->dialog_speaker = 0;
    set_button_caption(e, -1);          /* new game / load: the form's */
    event_engine_clear_actors(e);
    memset(e->moves, 0, sizeof(e->moves));
    e->hold_until = 0;
    memset(&e->cutscene, 0, sizeof(e->cutscene));
    memset(&e->wipe, 0, sizeof(e->wipe));
}
