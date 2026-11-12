#include "event_engine.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void engine_log(EventEngine *e, const char *fmt, const char *a)
{
    if (!e->log_fn)
        return;
    char buf[640];
    snprintf(buf, sizeof(buf), fmt, a ? a : "");
    e->log_fn(e->log_user, buf);
}

/* --------------------------------------------------------------------- */
/* zyouout (0x449e18): fill the dialogue window from ListBox2.            */

static void append_message(EventEngine *e, int index)
{
    const char *m = e->have_messages ? messages_get(&e->messages, index) : NULL;
    size_t len = strlen(e->dialog_text);
    if (len >= sizeof(e->dialog_text) - 1)
        return;
    if (m)
        snprintf(e->dialog_text + len, sizeof(e->dialog_text) - len, "%s", m);
    else
        snprintf(e->dialog_text + len, sizeof(e->dialog_text) - len,
                 "[message %d]", index);
}

static void zyouout(EventEngine *e)
{
    int id = e->aux.v[EVAUX_ROUJIN_AUX];             /* g_subevent_id */
    int sel = e->aux.v[EVAUX_UI_SELECTOR] & 0xffff;  /* word 0x48c5c8 */
    if (id == 9) {
        /* Game-over continuation (appends 0x16 etc.): not ported. */
        engine_log(e, "zyouout: sub-event 9 is not ported%s", NULL);
        return;
    }
    e->dialog_text[0] = '\0';
    if (id < 10) {
        append_message(e, e->aux.v[EVAUX_MESSAGE_SELECTOR] + 0x3e);
    } else {
        if (sel != 0x10 && sel != 0x16 && sel != 0x18 && sel != 0x28 &&
            sel != 0x2f)
            append_message(e, sel - 1);
        /* LStrCat (0x403658): these append a second entry. */
        if (sel == 5)
            append_message(e, sel + 0x34);
        else if (sel == 0x11)
            append_message(e, sel + 0x29);
        else if (sel == 0x37)
            append_message(e, sel + 4);
    }
    e->dialog_visible = true;
    e->dialog_open = true;
    e->dialog_choice = false;
    e->dialog_id = id;
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

static void opening9(EventEngine *e, int phase, int frame)
{
    EventCutscene *c = &e->cutscene;
    switch (phase) {
    case 0:                             /* open1 frames 0xe8..0xf7 */
        c->image = 2; c->overlay = 0; c->reveal = 0;
        break;
    case 2:                             /* open0 (launch intro) */
        c->image = 1; c->overlay = 0; c->reveal = 0;
        break;
    case 1:                             /* open2 revealed over open1 */
        if (frame == 400 || frame == 500) {
            memset(c, 0, sizeof(*c));   /* panels restored */
        } else {
            c->image = 2; c->overlay = 3;
            c->reveal = frame < 0 ? 0 : frame > 400 ? 400 : frame;
        }
        break;
    case 8:
        e->timer2 = false;              /* TTimer.SetEnabled(Timer2, False) */
        break;
    default:
        break;                          /* ending phases: not drawn yet */
    }
}

static void on_vm_log(EventVm *vm, const EventVmLog *entry, void *user)
{
    (void)vm;
    EventEngine *e = user;
    switch (entry->kind) {
    case EVLOG_THREE_SPRITE_ANIM:       /* op 0x09 at map index a */
        set_actor(e, entry->a, entry->b, entry->c, entry->d);
        break;
    case EVLOG_OPENING9:
        opening9(e, entry->a, entry->b);
        break;
    case EVLOG_HIDE_IMAGES:             /* 0x0b/0xe */
        memset(&e->cutscene, 0, sizeof(e->cutscene));
        break;
    case EVLOG_DOOR_OPEN:               /* a=index b=kind c=tile d=progress */
        if (entry->c >= 0)
            set_door(e, entry->a, entry->b, entry->d);
        break;
    case EVLOG_MONSTER_MOVE:            /* a=from b=to c=raw */
        start_move(e, entry->a, entry->b, entry->c);
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
        e->aux.v[EVAUX_ROUJIN_AUX] = sub + 10;
        /* word write (0x4531c9); the high word is itemlive's flag 0x48c5ca */
        e->aux.v[EVAUX_UI_SELECTOR] =
            (int32_t)(((uint32_t)e->aux.v[EVAUX_UI_SELECTOR] & 0xffff0000u) |
                      ((uint32_t)sub & 0xffffu));
        e->timer2 = false;
        zyouout(e);
        return;
    }
    /* 0x0b splice: the count is already decremented; the sub-handler's
     * builder appends over this command.  Unported ones hand over. */
    const char *name = splice_builder(sub);
    if (name && e->have_builders && event_builders_has(&e->builders, name)) {
        if (event_builders_run(&e->builders, name, &e->vm, &e->aux))
            return;
        engine_log(e, "splice builder failed: %s", e->builders.err);
    }
    snprintf(e->continuation, sizeof(e->continuation), "subdispatch:%d", sub);
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
    } else if (strcmp(kind, "status_text") == 0 && nargs > 0) {
        const char *m = e->have_messages ? messages_get(&e->messages, args[0])
                                         : NULL;
        if (m)
            snprintf(e->status_text, sizeof(e->status_text), "%s", m);
        else
            snprintf(e->status_text, sizeof(e->status_text), "[message %d]",
                     (int)args[0]);
        engine_log(e, "%s", e->status_text);
    } else if (strcmp(kind, "richedit_text") == 0 && nargs > 0) {
        e->dialog_text[0] = '\0';     /* RichEdit1.Text := ListBox2[id] */
        if (args[0] >= 0)
            append_message(e, args[0]);
    } else if (strcmp(kind, "richedit_show") == 0 && nargs > 0) {
        e->dialog_visible = args[0] != 0;
    } else if (strcmp(kind, "button1_show") == 0) {
        e->dialog_open = true;                 /* Button1 visible, focused */
        e->dialog_choice = false;
        e->dialog_id = e->aux.v[EVAUX_ROUJIN_AUX];
    } else if (strcmp(kind, "choice_show") == 0) {
        e->dialog_open = true;                 /* Button2 + Button7 */
        e->dialog_choice = true;
        e->choice_no_enabled = nargs > 0 && args[0] != 0;
        e->choice_focus_no = false;            /* Button2.SetFocus */
        e->dialog_id = e->aux.v[EVAUX_ROUJIN_AUX];
    } else if (strcmp(kind, "choice_hide") == 0) {
        e->dialog_choice = false;
        e->choice_no_enabled = false;
    } else if (strcmp(kind, "dialog_hide") == 0) {
        e->dialog_visible = false;             /* RichEdit1.Visible := False */
    } else if (strcmp(kind, "transitional") == 0 && nargs > 0) {
        snprintf(e->continuation, sizeof(e->continuation), "subevent:%d",
                 (int)args[0]);
    }
}

/* --------------------------------------------------------------------- */

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
    if (!builders_path || !*builders_path) {
        if (err && errsz) snprintf(err, errsz, "no builder script");
        return false;
    }
    e->have_builders = event_builders_init(&e->builders, builders_path,
                                           err, errsz);
    e->builders.present_fn = on_present;
    e->builders.present_user = e;
    return e->have_builders;
}

bool event_engine_load_messages(EventEngine *e, const char *path,
                                char *err, size_t errsz)
{
    if (e->have_messages)
        messages_free(&e->messages);
    e->have_messages = messages_load(&e->messages, path, err, errsz);
    return e->have_messages;
}

void event_engine_shutdown(EventEngine *e)
{
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
}

const char *event_engine_route(int floor, int index, uint8_t raw)
{
    (void)index;
    /* handan raw 0x07 -> mevent; only the floor-3 case is ported
     * (16-event-pilot.md, golden vectors handan_floor3*). */
    if (raw == 0x07 && floor == 3)
        return "handan";
    /* handan raw 0x19/0x1a -> roujin (0x44538a), the old men.  Ported for
     * every floor in its table; golden vectors roujin_* / handan_roujin_*. */
    if (raw == 0x19 || raw == 0x1a)
        return "handan";
    /* handan case 9 (0x4447df): the secret wall slides down (door kind 7).
     * Golden vectors handan_secret_wall_*. */
    if (raw == 0x09)
        return "handan";
    /* handan raw 0x13/0x14 -> syounin (0x445363), the merchants.  Golden
     * vectors syounin_* / button2click_* / button7click_*. */
    if (raw == 0x13 || raw == 0x14)
        return "handan";
    /* handan raw 0x15/0x16 -> touzoku (0x445370), the thief / floor-2
     * prisoner.  Golden vectors touzoku_* / handan_touzoku_*. */
    if (raw == 0x15 || raw == 0x16)
        return "handan";
    return NULL;
}

/* Engine-side state that builders change through present() and that the
 * builder runtime's fail-closed restore does not cover. */
typedef struct {
    bool timer2, dialog_visible, dialog_open, dialog_choice, choice_no_enabled;
    int dialog_id;
    char continuation[EVENT_ENGINE_KEY_LEN];
    char dialog_text[EVENT_ENGINE_TEXT_LEN];
} EngineSaved;

static void save_engine(const EventEngine *e, EngineSaved *s)
{
    s->timer2 = e->timer2;
    s->dialog_visible = e->dialog_visible;
    s->dialog_open = e->dialog_open;
    s->dialog_choice = e->dialog_choice;
    s->choice_no_enabled = e->choice_no_enabled;
    s->dialog_id = e->dialog_id;
    memcpy(s->continuation, e->continuation, sizeof(s->continuation));
    memcpy(s->dialog_text, e->dialog_text, sizeof(s->dialog_text));
}

static void restore_engine(EventEngine *e, const EngineSaved *s)
{
    e->timer2 = s->timer2;
    e->dialog_visible = s->dialog_visible;
    e->dialog_open = s->dialog_open;
    e->dialog_choice = s->dialog_choice;
    e->choice_no_enabled = s->choice_no_enabled;
    e->dialog_id = s->dialog_id;
    memcpy(e->continuation, s->continuation, sizeof(s->continuation));
    memcpy(e->dialog_text, s->dialog_text, sizeof(s->dialog_text));
}

EventEngineResult event_engine_trigger(EventEngine *e, int floor, int index,
                                       uint8_t raw, int direction)
{
    const char *builder = event_engine_route(floor, index, raw);
    if (!builder || !e->have_builders || !e->vm.state || !e->vm.map)
        return EVENT_ENGINE_NOT_ROUTED;
    if (event_engine_active(e))
        return EVENT_ENGINE_FAILED;
    EngineSaved saved;
    save_engine(e, &saved);
    e->aux.v[EVAUX_TARGET_INDEX] = index;
    e->aux.v[EVAUX_DIRECTION] = direction;
    e->continuation[0] = '\0';
    if (!event_builders_run(&e->builders, builder, &e->vm, &e->aux)) {
        restore_engine(e, &saved);
        engine_log(e, "event builder failed: %s", e->builders.err);
        return EVENT_ENGINE_FAILED;
    }
    e->ticking = false;
    return EVENT_ENGINE_STARTED;
}

bool event_engine_active(const EventEngine *e)
{
    return e->vm.count > 0 || e->dialog_open || e->continuation[0] != '\0';
}

const char *event_engine_dialog(const EventEngine *e)
{
    return e->dialog_visible ? e->dialog_text : NULL;
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
        /* Hand the sub-event to the transitional runtime. */
        snprintf(e->continuation, sizeof(e->continuation), "subevent:%d",
                 saved.dialog_id);
    }
    e->ticking = false;
    return true;
}

bool event_engine_update(EventEngine *e, uint64_t now)
{
    if (e->continuation[0])
        return true;                  /* paused for a transitional sub-event */
    if (!e->timer2 || e->vm.count <= 0) {
        bool drained = e->ticking && e->vm.count <= 0;
        e->ticking = false;
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
        int period = e->vm.interval_ms > 0 ? e->vm.interval_ms : 1;
        e->next_tick += (uint64_t)period;
        if (e->next_tick < e->hold_until)
            e->next_tick = e->hold_until;
        if (e->continuation[0])
            return true;
    }
    if (e->vm.count <= 0) {
        e->ticking = false;
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
    /* Moves expire on their own (and are tied to a floor), so the last
     * step of a timeline still slides after the engine goes idle. */
}

bool event_engine_take_continuation(EventEngine *e, char *out, size_t outsz)
{
    if (!e->continuation[0]) {
        if (out && outsz) out[0] = '\0';
        return false;
    }
    if (out && outsz)
        snprintf(out, outsz, "%s", e->continuation);
    e->continuation[0] = '\0';
    return true;
}

void event_engine_cancel(EventEngine *e)
{
    event_vm_clear(&e->vm);
    memset(&e->aux, 0, sizeof(e->aux));
    e->aux.v[EVAUX_DIRECTION] = 1;
    e->continuation[0] = '\0';
    e->ticking = false;
    e->timer2 = false;
    e->dialog_visible = false;
    e->dialog_open = false;
    e->dialog_choice = false;
    e->choice_no_enabled = false;
    e->dialog_id = 0;
    e->dialog_text[0] = '\0';
    event_engine_clear_actors(e);
    memset(e->moves, 0, sizeof(e->moves));
    e->hold_until = 0;
    memset(&e->cutscene, 0, sizeof(e->cutscene));
}
