/* The event engine's windows, where the original puts its controls in the
 * 640x400 layout (docs/re/tools/form_layout.py runs syokidata0 and the
 * window openers on the form; 19-windows.md lists the results), drawn in
 * the remake's plain style (widgets.c).  The game is played with the
 * keyboard only, so the original's button rows become choices with a
 * highlight bar and a dim line naming the keys; a two-way question opens
 * on its no-action answer. */

#include "render_windows.h"

#include "app.h"
#include "battle.h"
#include "items.h"
#include "itext.h"
#include "render.h"
#include "state.h"
#include "version.h"
#include "commit.h"     /* TSW_COMMIT (build/generated) */
#include "widgets.h"

#include <stdio.h>
#include <string.h>

/* Application.Title (0x488e74): ShowMessage's caption. */

static const char *list_text(const EventEngine *e, int index)
{
    return e->have_messages ? messages_get(&e->messages, index) : NULL;
}

static const char *code_text(const EventEngine *e, int index,
                             const char *dflt)
{
    const char *t = e->have_ui_texts ? messages_get(&e->ui_texts, index)
                                     : NULL;
    return t ? t : dflt;
}

static SDL_FRect rect(float x, float y, float w, float h)
{
    return (SDL_FRect){ x, y, w, h };
}

static TTF_Font *font(App *app, UiFont f)
{
    return app->assets.ui[f];
}

/* Two answers side by side, right-aligned at (right, y): the original's
 * Button2/Button7 spots.  `focus_second` highlights the second. */
static void draw_two_options(App *app, float right, float y,
                             const char *first, const char *second,
                             bool focus_second, bool second_enabled)
{
    SDL_Renderer *r = app->render.renderer;
    TTF_Font *f = font(app, UI_FONT_14);
    float w2 = ui_text_width(f, second) + 24.0f;
    float w1 = ui_text_width(f, first) + 24.0f;
    if (w2 < 64.0f) w2 = 64.0f;
    if (w1 < 64.0f) w1 = 64.0f;
    ui_option(r, f, rect(right - w2, y, w2, 25.0f), second,
              (focus_second ? UI_FOCUSED : 0) |
              (second_enabled ? 0 : UI_DISABLED), UI_CENTER);
    ui_option(r, f, rect(right - w2 - 8.0f - w1, y, w1, 25.0f), first,
              focus_second ? 0 : UI_FOCUSED, UI_CENTER);
}

/* --------------------------------------------------------------------- */
/* The message box at RichEdit1's bounds as zyouout places it: (129, 45)
 * 382x200.  Its answer line is where Button1 (420, 204) and Button2/7
 * are. */

#define RICH_X 129.0f
#define RICH_Y 45.0f
#define RICH_W 382.0f
#define RICH_H 200.0f
#define ANSWER_RIGHT (RICH_X + RICH_W - 16.0f)          /* 495 */
#define ANSWER_Y (RICH_Y + RICH_H - 25.0f - 16.0f)      /* 204 */

/* The message text in the size the Options menu chose (the original's
 * Font menu: Tiny / Small / Normal / Big, 10 / 11 / 12 / 14 pt there). */
static TTF_Font *message_font(App *app)
{
    static const UiFont BY_SIZE[OPT_TEXT_COUNT] = {
        UI_FONT_12, UI_FONT_13, UI_FONT_14, UI_FONT_16
    };
    OptTextSize t = app->options.text;
    return font(app, BY_SIZE[t >= 0 && t < OPT_TEXT_COUNT ? t
                                                         : OPT_TEXT_NORMAL]);
}

static void draw_rich_edit(App *app, SDL_FRect r, const char *text)
{
    SDL_Renderer *rr = app->render.renderer;
    SDL_FRect in = ui_panel(rr, r, 8.0f);
    ui_text_justified(rr, message_font(app), text, in, UI_TEXT);
}

/* The speaker's icon: frame A or B of their map tile, flipping with the
 * map's own animation (Timer1, imagework); while Timer1 is off, on a
 * clock of the same period. */
static bool speaker_frame_b(const App *app)
{
    const EventEngine *e = &app->events.engine;
    if (e->timer1)
        return e->shown_buffer == 1;
    int ms = event_vm_speed_interval_ms(e->vm.speed);
    if (ms <= 0)
        ms = 300;
    return (SDL_GetTicks() / (uint64_t)ms) & 1u;
}

static void draw_speaker_icon(App *app, int speaker, SDL_FRect box)
{
    SDL_Renderer *r = app->render.renderer;
    const SpeakerDef *d = speaker_def(speaker);
    if (!d)
        return;
    bool b = speaker_frame_b(app);
    if (d->frame_a < 0) {
        /* The hero: Player.png's facing-down frames (poses 0 and 1). */
        SDL_Texture *t = app->assets.player;
        if (!t)
            return;
        float w = 0.0f, h = 0.0f;
        SDL_GetTextureSize(t, &w, &h);
        int frame = w >= 12 * 32 ? (b ? 1 : 0) : 0;
        SDL_FRect src = { (float)(frame * 32), 0.0f, 32.0f, 32.0f };
        if (app->assets.tiles) {    /* the floor (map value 6) behind */
            SDL_FRect floor = { 5.0f * TSW_TILE_SIZE, 0.0f,
                                (float)TSW_TILE_SIZE, (float)TSW_TILE_SIZE };
            SDL_RenderTexture(r, app->assets.tiles, &floor, &box);
        }
        SDL_RenderTexture(r, t, &src, &box);
    } else {
        int tile = b ? d->frame_b : d->frame_a;
        SDL_Texture *t = app->assets.tiles;
        if (!t)
            return;
        SDL_FRect src = { (float)((tile % 11) * TSW_TILE_SIZE),
                          (float)((tile / 11) * TSW_TILE_SIZE),
                          (float)TSW_TILE_SIZE, (float)TSW_TILE_SIZE };
        SDL_RenderTexture(r, t, &src, &box);
    }
    ui_outline(r, rect(box.x - 1.0f, box.y - 1.0f, box.w + 2.0f, box.h + 2.0f),
               UI_LINE);
}

static void draw_hint_line(App *app, const char *hint)
{
    ui_hint(app->render.renderer, font(app, UI_FONT_12), hint,
            rect(RICH_X + 12.0f, ANSWER_Y, 220.0f, 25.0f), UI_LEFT);
}

static void draw_message_window(App *app)
{
    const EventEngine *e = &app->events.engine;
    SDL_Renderer *r = app->render.renderer;
    TTF_Font *f = font(app, UI_FONT_14);
    const char *msg = event_engine_dialog(e);
    if (!msg)
        return;
    int speaker = event_engine_dialog_speaker(e);
    const char *name = speaker >= 0 ? event_engine_speaker_name(e, speaker)
                                    : NULL;
    if (name && !e->altar_open) {
        /* A speaker: their name across the top, their animated map
         * sprite (x2) on the left and the words beside it. */
        SDL_FRect win = rect(RICH_X, RICH_Y, RICH_W, RICH_H);
        ui_panel(r, win, 0.0f);
        ui_text_in(r, font(app, UI_FONT_14B), name,
                   rect(RICH_X + 12.0f, RICH_Y + 4.0f, RICH_W - 24.0f, 22.0f),
                   UI_LEFT, 0.0f, UI_TITLE);
        ui_fill(r, rect(RICH_X + 1.0f, RICH_Y + 27.0f, RICH_W - 2.0f, 1.0f),
                UI_LINE);
        SDL_FRect icon = rect(RICH_X + 10.0f, RICH_Y + 37.0f, 64.0f, 64.0f);
        draw_speaker_icon(app, speaker, icon);
        ui_text_justified(r, message_font(app), msg,
                          rect(RICH_X + 86.0f, RICH_Y + 34.0f,
                               RICH_W - 94.0f, RICH_H - 42.0f),
                          UI_TEXT);
    } else {
        draw_rich_edit(app, rect(RICH_X, RICH_Y, RICH_W, RICH_H), msg);
    }
    if (e->altar_open) {
        /* Button2Click mode 1: the three offers and cancel (Button39..42)
         * as a list from (145, 93), cancel highlighted first. */
        static const char *KEY[4] = { "1", "2", "3", "Esc" };
        for (int i = 0; i < 4; i++) {
            SDL_FRect row = rect(RICH_X + 16.0f,
                                 RICH_Y + 48.0f + 32.0f * (float)i, 350.0f,
                                 28.0f);
            ui_option(r, f, rect(row.x + 36.0f, row.y, row.w - 36.0f, row.h),
                      e->altar_options[i],
                      e->altar_focus == i ? UI_FOCUSED : 0, UI_LEFT);
            ui_hint(r, font(app, UI_FONT_12), KEY[i],
                    rect(row.x, row.y, 30.0f, row.h), UI_RIGHT);
        }
        return;
    }
    if (e->dialog_open && e->dialog_choice) {
        draw_hint_line(app, e->choice_no_enabled
                                ? tx(ITX_HINT_YES_NO)
                                : tx(ITX_HINT_ENTER));
        draw_two_options(app, ANSWER_RIGHT, ANSWER_Y, tx(ITX_ANSWER_YES), tx(ITX_ANSWER_NO),
                         e->choice_focus_no, e->choice_no_enabled);
    } else if (e->dialog_open) {
        /* Button1's caption: "OK", "What?", "Hey!" (the event's). */
        const char *cap = event_engine_button_caption(e);
        if (strcmp(cap, "OK") == 0)     /* the form's own caption */
            cap = tx(ITX_ANSWER_OK);
        float w = ui_text_width(f, cap) + 24.0f;
        if (w < 64.0f)
            w = 64.0f;
        draw_hint_line(app, tx(ITX_HINT_ENTER));
        ui_option(r, f, rect(ANSWER_RIGHT - w, ANSWER_Y, w, 25.0f), cap,
                  UI_FOCUSED, UI_CENTER);
    }
}

/* --------------------------------------------------------------------- */
/* The left panel's item grid after syokidata0: slot s at
 * (14 + 32 * (s % 3), 192 + 32 * (s / 3)). */

static float slot_x(int slot) { return 14.0f + 32.0f * (float)(slot % 3); }
static float slot_y(int slot) { return 192.0f + 32.0f * (float)(slot / 3); }

/* A box across the left panel under `below_y` (or above `above_y` when it
 * would run off the panel). */
static SDL_FRect left_box(float below_y, float above_y, float h)
{
    float y = below_y + 2.0f;
    if (y + h > 396.0f)
        y = above_y - 2.0f - h;
    return rect(6.0f, y, 113.0f, h);
}

/* img8work .. img17work: the original shows "Don't use" / "Use" under the
 * item's picture.  The remake frames the item and asks under it. */
static void draw_item_panel(App *app)
{
    static const int CASE_SLOT[15] = {
        -1, -1, -1, -1, -1, 4, 5, 6, 7, 8, 9, -1, 11, 12, 13
    };
    const EventEngine *e = &app->events.engine;
    int c = e->item_confirm_case;
    int slot = c >= 0 && c < 15 ? CASE_SLOT[c] : -1;
    if (slot < 0)
        return;
    SDL_Renderer *r = app->render.renderer;
    TTF_Font *f = font(app, UI_FONT_12);
    float x = slot_x(slot), y = slot_y(slot);
    ui_outline(r, rect(x - 1.0f, y - 1.0f, 34.0f, 34.0f), UI_HOT);
    SDL_FRect box = left_box(y + 32.0f, y, 58.0f);
    SDL_FRect in = ui_panel(r, box, 4.0f);
    TTF_Font *b[2] = { font(app, UI_FONT_12B), font(app, UI_FONT_12) };
    const char *name = special_item_name(slot);
    ui_text_in(r, ui_fit(b, 2, name, in.w), name,
               rect(in.x, in.y, in.w, 16.0f), UI_CENTER, 0.0f, UI_TITLE);
    bool no = e->item_confirm_focus_no;
    ui_option(r, f, rect(in.x, in.y + 18.0f, 36.0f, 18.0f), tx(ITX_ANSWER_USE),
              no ? 0 : UI_FOCUSED, UI_CENTER);
    ui_option(r, f, rect(in.x + 40.0f, in.y + 18.0f, in.w - 40.0f, 18.0f),
              tx(ITX_ANSWER_DONT_USE), no ? UI_FOCUSED : 0, UI_CENTER);
    ui_hint(r, f, tx(ITX_HINT_USE), rect(in.x, in.y + 37.0f, in.w, 13.0f),
            UI_CENTER);
}

/* img4work: the original's UP / DOWN / OK buttons at (70, 224..256).  The
 * map already follows the floor chosen; the box shows the floor and the
 * directions open. */
static void draw_fly_panel(App *app)
{
    const EventEngine *e = &app->events.engine;
    SDL_Renderer *r = app->render.renderer;
    SDL_FRect in = ui_panel(r, rect(6.0f, 224.0f, 113.0f, 52.0f), 4.0f);
    char buf[32];
    snprintf(buf, sizeof(buf), tx(ITX_FLY_FLOOR),
             state_get(&app->game.state, ST_FLOOR));
    SDL_FRect line = rect(in.x, in.y, in.w, 22.0f);
    ui_text_in(r, font(app, UI_FONT_14B), buf, line, UI_CENTER, 0.0f,
               UI_HOT);
    ui_triangle(r, rect(in.x + 2.0f, line.y + 4.0f, 14.0f, 14.0f), true,
                e->fly_up_enabled ? UI_TEXT : UI_LINE);
    ui_triangle(r, rect(in.x + in.w - 16.0f, line.y + 4.0f, 14.0f, 14.0f),
                false, e->fly_down_enabled ? UI_TEXT : UI_LINE);
    ui_hint(r, font(app, UI_FONT_12), tx(ITX_HINT_FLY),
            rect(in.x, in.y + 26.0f, in.w, 16.0f), UI_CENTER);
}

/* img5work: the original's Button11..34 ("1".."24", enabled for the men
 * heard) and OK.  The remake lists the men heard; the highlighted one's
 * words show in the message box at once. */
static void draw_wisdom_window(App *app)
{
    const EventEngine *e = &app->events.engine;
    SDL_Renderer *r = app->render.renderer;
    TTF_Font *f = font(app, UI_FONT_12);
    SDL_FRect in = ui_panel(r, rect(6.0f, 192.0f, 113.0f, 202.0f), 4.0f);
    int heard[24], n = 0, at = 0;
    for (int k = 0; k < 24; k++)
        if (e->wisdom_enabled[k]) {
            if (k == e->wisdom_focus)
                at = n;
            heard[n++] = k;
        }
    const float row_h = 17.0f;
    const int rows = 9;
    if (n == 0) {
        ui_text_wrapped(r, f, tx(ITX_WISDOM_NONE),
                        rect(in.x + 2.0f, in.y + 2.0f, in.w - 4.0f, 40.0f),
                        UI_DIM);
    } else {
        int first = at - rows / 2;
        if (first > n - rows) first = n - rows;
        if (first < 0) first = 0;
        for (int i = first; i < n && i < first + rows; i++) {
            char buf[64];
            snprintf(buf, sizeof(buf), tx(ITX_WISDOM_OLD_MAN), heard[i] + 1);
            ui_option(r, f, rect(in.x, in.y + row_h * (float)(i - first),
                                 in.w, row_h - 1.0f),
                      buf, heard[i] == e->wisdom_focus ? UI_FOCUSED : 0,
                      UI_LEFT);
        }
        if (first > 0)
            ui_triangle(r, rect(in.x + in.w - 12.0f, in.y + 2.0f, 8.0f, 8.0f),
                        true, UI_DIM);
        if (first + rows < n)
            ui_triangle(r, rect(in.x + in.w - 12.0f,
                                in.y + row_h * (float)rows - 10.0f, 8.0f,
                                8.0f), false, UI_DIM);
    }
    ui_hint(r, f, tx(ITX_HINT_WISDOM),
            rect(in.x, in.y + in.h - 16.0f, in.w, 16.0f), UI_CENTER);
    const char *msg = event_engine_dialog(e);
    if (msg)
        draw_rich_edit(app, rect(RICH_X, RICH_Y, RICH_W, RICH_H), msg);
}

/* --------------------------------------------------------------------- */
/* MessageDlg / ShowMessage: a separate dialog centred on the playfield. */

static void draw_message_dialog(App *app, const char *title,
                                const char *text, bool yes_no, bool focus_no)
{
    SDL_Renderer *r = app->render.renderer;
    TTF_Font *f = font(app, UI_FONT_14);
    const float max_w = 380.0f;
    float tw = ui_text_width(f, text);
    int lines = tw > max_w ? (int)(tw / max_w) + 1 : 1;
    if (tw > max_w)
        tw = max_w;
    float th = (float)TTF_GetFontLineSkip(f) * (float)lines;
    float w = (tw > 260.0f ? tw : 260.0f) + 48.0f;
    float h = 34.0f + th + 16.0f + 25.0f + 16.0f;
    SDL_FRect win = rect((float)(int)((640.0f - w) / 2.0f),
                         (float)(int)((400.0f - h) / 2.0f), w, h);
    SDL_FRect c = ui_dialog(r, font(app, UI_FONT_14B), win, title,
                            (float)TSW_CANVAS_W, (float)TSW_CANVAS_H);
    ui_text_wrapped(r, f, text, rect(c.x + (c.w - tw) / 2.0f, c.y, tw + 1.0f,
                                     th + 2.0f), UI_TEXT);
    float y = c.y + th + 16.0f;
    float right = win.x + win.w - 12.0f;
    ui_hint(r, font(app, UI_FONT_12),
            yes_no ? tx(ITX_HINT_YES_NO) : tx(ITX_HINT_ENTER),
            rect(c.x, y, 160.0f, 25.0f), UI_LEFT);
    if (yes_no)
        draw_two_options(app, right, y, tx(ITX_ANSWER_YES), tx(ITX_ANSWER_NO), focus_no, true);
    else
        ui_option(r, f, rect(right - 64.0f, y, 64.0f, 25.0f), tx(ITX_ANSWER_OK),
                  UI_FOCUSED, UI_CENTER);
}

/* --------------------------------------------------------------------- */
/* Form2 (the ranking): 480x444 centred, the controls at formactivate's
 * bounds (0x434bc4) from the panel's top-left; Memo61's text is the
 * title.  The area buttons are tabs (Left/Right); OK and Clear are left
 * out (Enter closes). */

static void draw_ranking(App *app)
{
    const EventEngine *e = &app->events.engine;
    SDL_Renderer *r = app->render.renderer;
    TTF_Font *f12 = font(app, UI_FONT_12);
    TTF_Font *f14 = font(app, UI_FONT_14);
    TTF_Font *f14b = font(app, UI_FONT_14B);
    SDL_FRect win = rect(80.0f, 18.0f, 480.0f, 444.0f);
    ui_veil(r, (float)TSW_CANVAS_W, (float)TSW_CANVAS_H);
    ui_panel(r, win, 0.0f);
    float ox = win.x + 3.0f, oy = win.y + 12.0f;
    ui_text_in(r, font(app, UI_FONT_16B), tx(ITX_RANKING_TITLE),
               rect(ox + 16.0f, oy + 8.0f, 442.0f, 28.0f), UI_CENTER, 0.0f,
               UI_TITLE);
    static const char *AREAS[5] = { "10F", "20F", "25F", "40F", "49F" };
    for (int i = 0; i < 5; i++) {
        SDL_FRect b = rect(ox + 49.0f + 80.0f * (float)i, oy + 44.0f, 56.0f,
                           56.0f);
        bool hot = e->ranking_view_area == i + 1;
        if (hot) {
            ui_fill(r, b, UI_BUTTON_HOT);
            ui_outline(r, b, UI_HOT);
        }
        float th = (float)TTF_GetFontHeight(f12);
        float top = b.y + (b.h - (32.0f + 4.0f + th)) / 2.0f;
        SDL_Texture *glyph = app->assets.rank_glyph[i];
        if (glyph) {
            SDL_FRect g = { b.x + 12.0f, (float)(int)top, 32.0f, 32.0f };
            SDL_RenderTexture(r, glyph, NULL, &g);
        }
        ui_text_in(r, f12, AREAS[i], rect(b.x, top + 36.0f, b.w, th),
                   UI_CENTER, 0.0f, hot ? UI_HOT : UI_DIM);
    }
    static const float COL_X[6] = { 16, 64, 127, 279, 349, 404 };
    static const float COL_W[6] = { 40, 55, 152, 70, 55, 55 };
    const char *HEAD[6] = {
        tx(ITX_RANKING_RANK), tx(ITX_RANKING_POINTS), tx(ITX_RANKING_NAME),
        tx(ITX_RANKING_LIFE), tx(ITX_RANKING_OFFENSE), tx(ITX_RANKING_DEFENSE)
    };
    static const int ALIGN[6] = { UI_CENTER, UI_RIGHT, UI_LEFT,
                                  UI_RIGHT, UI_RIGHT, UI_RIGHT };
    for (int col = 0; col < 6; col++)
        ui_text_in(r, f12, HEAD[col],
                   rect(ox + COL_X[col], oy + 108.0f, COL_W[col], 16.0f),
                   ALIGN[col], 4.0f, UI_DIM);
    const Ranking *rk = event_engine_ranking_shown(e);
    for (int row = 0; row < RANKING_ROWS; row++) {
        const int32_t *v = &rk->v[row * 6];
        char cells[6][RANKING_NAME_LEN + 8];
        snprintf(cells[0], sizeof(cells[0]), "%d", row + 1);
        snprintf(cells[1], sizeof(cells[1]), "%d", (int)v[0]);
        snprintf(cells[2], sizeof(cells[2]), "%s", rk->names[row]);
        snprintf(cells[3], sizeof(cells[3]), "%d", (int)v[1]);
        snprintf(cells[4], sizeof(cells[4]), "%d", (int)v[2]);
        snprintf(cells[5], sizeof(cells[5]), "%d", (int)v[3]);
        float y = oy + 126.0f + 25.0f * (float)row;
        if (row % 2)
            ui_fill(r, rect(ox + 16.0f, y, 443.0f, 25.0f), UI_PANEL_ALT);
        /* The first row stands out, as the original's red row does. */
        for (int col = 0; col < 6; col++)
            ui_text_in(r, f14b, cells[col],
                       rect(ox + COL_X[col], y, COL_W[col], 25.0f),
                       ALIGN[col], 4.0f, row == 0 ? UI_HOT : UI_TEXT);
    }
    if (!e->ranking_name_entry)
        ui_hint(r, f12, tx(ITX_HINT_RANKING),
                rect(ox + 16.0f, oy + 384.0f, 443.0f, 25.0f), UI_CENTER);

    if (e->ranking_name_entry) {
        /* InputQuery: the prompt and the name. */
        char buf[RANKING_NAME_LEN * 2 + 8];
        snprintf(buf, sizeof(buf), "%s%s_", app->events.name_input,
                 app->events.name_edit);
        SDL_FRect q = rect(170.0f, 150.0f, 300.0f, 126.0f);
        SDL_FRect qc = ui_dialog(r, f14b, q,
                                 code_text(e, 6, "Enter your name"),
                                 (float)TSW_CANVAS_W, (float)TSW_CANVAS_H);
        ui_text(r, f14, code_text(e, 7, "Use at most 18 characters."),
                qc.x, qc.y, UI_TEXT);
        SDL_FRect ed = rect(qc.x, qc.y + 24.0f, qc.w, 24.0f);
        ui_fill(r, ed, UI_BUTTON);
        ui_outline(r, ed, UI_BORDER);
        ui_text_in(r, f14, buf, ed, UI_LEFT, 6.0f, UI_HOT);
        if (app->events.name_edit[0]) {
            /* the input method's unconfirmed text, underlined */
            float x0 = ed.x + 6.0f + ui_text_width(f14, app->events.name_input);
            float x1 = x0 + ui_text_width(f14, app->events.name_edit);
            ui_fill(r, rect(x0, ed.y + ed.h - 4.0f, x1 - x0, 1.0f), UI_HOT);
        }
        ui_hint(r, f12, tx(ITX_HINT_NAME),
                rect(qc.x, qc.y + 54.0f, qc.w, 20.0f), UI_CENTER);
    }
}

/* --------------------------------------------------------------------- */

void render_windows_draw(App *app)
{
    const EventEngine *e = &app->events.engine;
    if (e->ranking_open)
        draw_ranking(app);
    if (e->message_open) {
        draw_message_dialog(app, TSW_TITLE, e->message_text, false, false);
        return;
    }
    if (e->ranking_open)
        return;
    if (e->death_prompt) {
        /* MessageDlg(mtInformation, [mbYes, mbNo]). */
        draw_message_dialog(app, tx(ITX_TITLE_INFORMATION),
                            e->death_prompt_text[0] ? e->death_prompt_text
                                                    : "You have no vital power left. Try again?",
                            true, e->choice_focus_no);
        return;
    }
    if (e->item_confirm) {
        draw_item_panel(app);
        return;
    }
    if (e->fly_open) {
        draw_fly_panel(app);
        return;
    }
    if (e->wisdom_open) {
        draw_wisdom_window(app);
        return;
    }
    draw_message_window(app);
}

/* --------------------------------------------------------------------- */
/* img6work / yousya: a panel over the map (Panel1, 125..515 x 0..397);
 * the caption (ListBox2[0xb4]) where RichEdit1 is, at (141, 16) 358x56;
 * seven rows 40 px apart from y 80: the picture at (141, y + 4), the name
 * (181, y, 114 wide), the damage (295, y, 204 wide) and the stats line
 * (181, y + 20, 318 wide).  Button36 (OK) becomes the key line. */

void render_hero_orb(App *app)
{
    const EventEngine *e = &app->events.engine;
    SDL_Renderer *r = app->render.renderer;
    TTF_Font *bold[2] = { font(app, UI_FONT_14B), font(app, UI_FONT_12B) };
    TTF_Font *plain[2] = { font(app, UI_FONT_14), font(app, UI_FONT_12) };
    ui_panel(r, rect(125.0f, 0.0f, 390.0f, 397.0f), 0.0f);
    const char *caption = list_text(e, 0xb4);
    ui_text_wrapped(r, font(app, UI_FONT_14), caption ? caption : "",
                    rect(141.0f, 20.0f, 358.0f, 52.0f), UI_TITLE);
    long long d = (long long)state_get(&app->game.state, ST_OPTION_A) + 1;
    for (int i = 0; i < 7; i++) {
        float y = 80.0f + 40.0f * (float)i;
        SDL_SetRenderDrawColor(r, UI_LINE.r, UI_LINE.g, UI_LINE.b, 255);
        SDL_RenderLine(r, 141.0f, y - 1.0f, 499.0f, y - 1.0f);
        int index = i < e->hero_rows ? e->hero_row_index[i] : -1;
        render_panel_icon(app, index >= 0 ? event_engine_monster_picture(index)
                                          : -1, 141.0f, y + 4.0f);
        if (index < 0 || index >= MONSTER_COUNT)
            continue;
        const MonsterDef *m = &MONSTER_TABLE[index];
        const char *nm = list_text(e, event_engine_monster_name_id(index));
        if (!nm)
            nm = m->name;
        SDL_FRect name = rect(181.0f, y, 114.0f, 20.0f);
        ui_text_in(r, ui_fit(bold, 2, nm, name.w - 4.0f), nm, name, UI_LEFT,
                   2.0f, UI_TEXT);
        char buf[160];
        int damage = e->hero_row_damage[i];
        const char *label = list_text(e, damage >= 0 ? 0xb8 : 0xb9);
        if (damage >= 0)
            snprintf(buf, sizeof(buf), "%s %d", label ? label : "", damage);
        else
            snprintf(buf, sizeof(buf), "%s", label ? label : "");
        SDL_FRect dmg = rect(295.0f, y, 204.0f, 20.0f);
        ui_text_in(r, ui_fit(bold, 2, buf, dmg.w - 4.0f), buf, dmg, UI_LEFT,
                   2.0f, damage >= 0 ? UI_HOT : UI_WARN);
        const char *l = list_text(e, 0xd6), *o = list_text(e, 0xd7),
                   *df = list_text(e, 0xd8);
        /* yousya's sixth row leaves out the space after "Offense". */
        snprintf(buf, sizeof(buf), "%s %lld %s%s%lld %s %lld", l ? l : "",
                 m->hp * d, o ? o : "", i == 5 ? "" : " ", m->attack * d,
                 df ? df : "", m->defense * d);
        SDL_FRect stats = rect(181.0f, y + 20.0f, 318.0f, 20.0f);
        ui_text_in(r, ui_fit(plain, 2, buf, stats.w - 4.0f), buf, stats,
                   UI_LEFT, 2.0f, UI_TEXT);
    }
    ui_hint(r, font(app, UI_FONT_12), tx(ITX_HINT_CLOSE),
            rect(141.0f, 368.0f, 358.0f, 22.0f), UI_RIGHT);
}

/* --------------------------------------------------------------------- */
/* The menu bar (Game / Options / Help), Save / Load, the confirmations,
 * Controls and About: separate dialogs over the dimmed screen. */


#define MENU_ROW 26.0f

static void option_value(App *app, int index, char *out, size_t outsz)
{
    const Options *o = &app->options;
    switch (index) {
    case UI_OPT_SOUND: snprintf(out, outsz, "%s", tx(o->sound ? ITX_VALUE_ON : ITX_VALUE_OFF)); break;
    case UI_OPT_MUSIC: snprintf(out, outsz, "%s", tx(o->music ? ITX_VALUE_ON : ITX_VALUE_OFF)); break;
    case UI_OPT_SPEED: snprintf(out, outsz, "%s", options_speed_label(o->speed)); break;
    case UI_OPT_TEXT: snprintf(out, outsz, "%s", options_text_label(o->text)); break;
    case UI_OPT_LANGUAGE: snprintf(out, outsz, "%s", app->language_name); break;
    default: out[0] = '\0'; break;
    }
}

static void draw_menu_bar(App *app)
{
    SDL_Renderer *r = app->render.renderer;
    const UiState *ui = &app->ui;
    TTF_Font *f = font(app, UI_FONT_14);
    const float w = 300.0f;
    int rows = ui_tab_count(ui->menu_tab);
    float h = 12.0f + 30.0f + 10.0f + MENU_ROW * 5.0f + 34.0f;
    SDL_FRect win = rect((640.0f - w) / 2.0f, (400.0f - h) / 2.0f, w, h);
    ui_veil(r, (float)TSW_CANVAS_W, (float)TSW_CANVAS_H);
    ui_panel(r, win, 0.0f);
    /* The menus side by side; the open one highlighted. */
    float tab_w = (w - 24.0f) / (float)UI_TAB_COUNT;
    for (int t = 0; t < UI_TAB_COUNT; t++)
        ui_option(r, font(app, UI_FONT_14B),
                  rect(win.x + 12.0f + tab_w * (float)t, win.y + 12.0f,
                       tab_w - 4.0f, 28.0f),
                  ui_tab_label(t), t == ui->menu_tab ? UI_FOCUSED : 0,
                  UI_CENTER);
    SDL_SetRenderDrawColor(r, UI_LINE.r, UI_LINE.g, UI_LINE.b, 255);
    SDL_RenderLine(r, win.x + 12.0f, win.y + 46.0f, win.x + w - 12.0f,
                   win.y + 46.0f);
    float y = win.y + 52.0f;
    for (int i = 0; i < rows; i++) {
        SDL_FRect row = rect(win.x + 12.0f, y + MENU_ROW * (float)i,
                             w - 24.0f, MENU_ROW - 2.0f);
        int flags = i == ui->menu_selected ? UI_FOCUSED : 0;
        if (ui->menu_tab == UI_TAB_GAME && i == UI_GAME_SAVE &&
            state_get(&app->game.state, ST_LIFE) <= 0)
            flags |= UI_DISABLED;
        ui_option(r, f, row, ui_menu_label(ui->menu_tab, i), flags, UI_LEFT);
        if (ui->menu_tab == UI_TAB_OPTIONS) {
            char v[32];
            option_value(app, i, v, sizeof(v));
            ui_text_in(r, f, v, row, UI_RIGHT, 8.0f,
                       i == ui->menu_selected ? UI_HOT : UI_TITLE);
        }
    }
    ui_hint(r, font(app, UI_FONT_12),
            ui->menu_tab == UI_TAB_OPTIONS
                ? tx(ITX_HINT_MENU_OPTIONS)
                : tx(ITX_HINT_MENU),
            rect(win.x, win.y + h - 28.0f, w, 20.0f), UI_CENTER);
}

static void draw_slots(App *app, bool load)
{
    SDL_Renderer *r = app->render.renderer;
    const UiState *ui = &app->ui;
    TTF_Font *f = font(app, UI_FONT_14);
    int rows = load ? ui_load_rows(ui) : UI_SLOT_COUNT;
    const float w = 340.0f;
    float h = 34.0f + MENU_ROW * (float)rows + 36.0f;
    SDL_FRect win = rect((640.0f - w) / 2.0f, (400.0f - h) / 2.0f, w, h);
    SDL_FRect c = ui_dialog(r, font(app, UI_FONT_14B), win,
                            tx(load ? ITX_TITLE_LOAD : ITX_TITLE_SAVE),
                            (float)TSW_CANVAS_W,
                            (float)TSW_CANVAS_H);
    for (int i = 0; i < rows; i++) {
        int slot = load ? ui_load_row_slot(ui, i) : i + 1;
        char name[64];
        if (slot == 0)
            snprintf(name, sizeof(name), "%s", tx(ITX_SLOT_CHECKPOINT));
        else
            snprintf(name, sizeof(name), tx(ITX_SLOT_NUMBER), slot);
        const char *info = app->slot_info[slot];
        SDL_FRect row = rect(c.x, c.y + MENU_ROW * (float)i, c.w,
                             MENU_ROW - 2.0f);
        int flags = i == ui->slot_selected ? UI_FOCUSED : 0;
        if (load && !info[0])
            flags |= UI_DISABLED;
        ui_option(r, f, row, name, flags, UI_LEFT);
        ui_text_in(r, font(app, UI_FONT_12), info[0] ? info : tx(ITX_SLOT_EMPTY), row,
                   UI_RIGHT, 8.0f, info[0] ? UI_TITLE : UI_DIM);
    }
    ui_hint(r, font(app, UI_FONT_12),
            tx(ITX_HINT_SLOTS),
            rect(win.x, win.y + h - 28.0f, w, 20.0f), UI_CENTER);
}

static void draw_confirm(App *app, const char *title, const char *text)
{
    SDL_Renderer *r = app->render.renderer;
    TTF_Font *f = font(app, UI_FONT_14);
    float w = ui_text_width(f, text) + 48.0f;
    if (w < 300.0f)
        w = 300.0f;
    const float h = 34.0f + 20.0f + 16.0f + 25.0f + 16.0f;
    SDL_FRect win = rect((float)(int)((640.0f - w) / 2.0f),
                         (float)(int)((400.0f - h) / 2.0f), w, h);
    SDL_FRect c = ui_dialog(r, font(app, UI_FONT_14B), win, title,
                            (float)TSW_CANVAS_W, (float)TSW_CANVAS_H);
    ui_text_in(r, f, text, rect(c.x, c.y, c.w, 20.0f), UI_CENTER, 0.0f,
               UI_TEXT);
    float y = c.y + 36.0f;
    ui_hint(r, font(app, UI_FONT_12),
            tx(ITX_HINT_YES_NO), rect(c.x, y, 160.0f, 25.0f), UI_LEFT);
    draw_two_options(app, win.x + win.w - 12.0f, y, tx(ITX_ANSWER_YES), tx(ITX_ANSWER_NO),
                     !app->ui.confirm_yes, true);
}

static void draw_text_page(App *app, const char *title,
                           const char *const *lines, int count, float w)
{
    SDL_Renderer *r = app->render.renderer;
    TTF_Font *f = font(app, UI_FONT_14);
    float h = 34.0f + 22.0f * (float)count + 40.0f;
    SDL_FRect win = rect((640.0f - w) / 2.0f, (400.0f - h) / 2.0f, w, h);
    SDL_FRect c = ui_dialog(r, font(app, UI_FONT_14B), win, title,
                            (float)TSW_CANVAS_W, (float)TSW_CANVAS_H);
    for (int i = 0; i < count; i++) {
        const char *line = lines[i];
        const char *tab = strchr(line, '\t');
        SDL_FRect row = rect(c.x, c.y + 22.0f * (float)i, c.w, 20.0f);
        if (tab) {                       /* "keys\twhat they do" */
            char keys[64];
            size_t n = (size_t)(tab - line) < sizeof(keys) - 1
                           ? (size_t)(tab - line) : sizeof(keys) - 1;
            memcpy(keys, line, n);
            keys[n] = '\0';
            ui_text_in(r, font(app, UI_FONT_14B), keys,
                       rect(row.x, row.y, 150.0f, row.h), UI_LEFT, 0.0f,
                       UI_HOT);
            ui_text_in(r, f, tab + 1,
                       rect(row.x + 150.0f, row.y, row.w - 150.0f, row.h),
                       UI_LEFT, 0.0f, UI_TEXT);
        } else {
            ui_text_in(r, f, line, row, UI_CENTER, 0.0f,
                       i == 0 ? UI_TITLE : UI_TEXT);
        }
    }
    ui_hint(r, font(app, UI_FONT_12), tx(ITX_HINT_BACK),
            rect(win.x, win.y + h - 28.0f, w, 20.0f), UI_CENTER);
}

/* Help > About: the name and version, what the remake is of, and the
 * credits (NAO's page, wwajp.com/mtower/, and the Windows version's
 * Readme.txt).  Each line has its own size and colour; an empty line is a
 * gap. */
static void draw_about(App *app)
{
    char commit[64];
    snprintf(commit, sizeof(commit), tx(ITX_ABOUT_COMMIT), TSW_COMMIT);
    const struct { UiFont font; int colour; const char *text; } L[] = {
        { UI_FONT_16B, 2, TSW_TITLE },
        { UI_FONT_12, 1, commit },
        { UI_FONT_12, 0, "" },
        { UI_FONT_14, 0, tx(ITX_ABOUT_REMAKE) },
        { UI_FONT_14, 0, tx(ITX_ABOUT_AUTHOR) },
        { UI_FONT_12, 0, "" },
        { UI_FONT_13, 0, tx(ITX_ABOUT_TEXT_REVIEW) },
        { UI_FONT_13, 0, tx(ITX_ABOUT_TRANSLATIONS) },
        { UI_FONT_13, 0, tx(ITX_ABOUT_TRANSLATION_LANGS) },
        { UI_FONT_12, 0, "" },
        { UI_FONT_13, 0, tx(ITX_ABOUT_ORIGINAL) },
        { UI_FONT_13, 0, tx(ITX_ABOUT_WINDOWS) },
        { UI_FONT_13, 0, tx(ITX_ABOUT_TEXT_CHECK) },
        { UI_FONT_12, 0, "" },
        { UI_FONT_13, 1, tx(ITX_ABOUT_LEGACY_1) },
        { UI_FONT_13, 1, tx(ITX_ABOUT_LEGACY_2) },
        { UI_FONT_13, 1, tx(ITX_ABOUT_FAN_WORKS) },
    };
    const SDL_Color colours[3] = { UI_TEXT, UI_DIM, UI_TITLE };
    const int count = (int)(sizeof(L) / sizeof(L[0]));
    SDL_Renderer *r = app->render.renderer;
    float lines_h = 0.0f;
    for (int i = 0; i < count; i++)
        lines_h += L[i].text[0]
                       ? (float)TTF_GetFontLineSkip(font(app, L[i].font)) + 2.0f
                       : 8.0f;
    const float w = 420.0f;
    float h = 34.0f + lines_h + 40.0f;
    SDL_FRect win = rect((float)(int)((640.0f - w) / 2.0f),
                         (float)(int)((400.0f - h) / 2.0f), w, h);
    SDL_FRect c = ui_dialog(r, font(app, UI_FONT_14B), win, tx(ITX_TITLE_ABOUT),
                            (float)TSW_CANVAS_W, (float)TSW_CANVAS_H);
    float y = c.y;
    for (int i = 0; i < count; i++) {
        if (!L[i].text[0]) {
            y += 8.0f;
            continue;
        }
        TTF_Font *f = font(app, L[i].font);
        float lh = (float)TTF_GetFontLineSkip(f) + 2.0f;
        ui_text_in(r, f, L[i].text, rect(c.x, y, c.w, lh), UI_CENTER, 0.0f,
                   colours[L[i].colour]);
        y += lh;
    }
    ui_hint(r, font(app, UI_FONT_12), tx(ITX_HINT_BACK),
            rect(win.x, win.y + h - 28.0f, w, 20.0f), UI_CENTER);
}

void render_menu(App *app)
{
    const UiState *ui = &app->ui;
    switch (ui->view) {
    case UI_VIEW_MENU:
        draw_menu_bar(app);
        break;
    case UI_VIEW_SAVE:
        draw_slots(app, false);
        break;
    case UI_VIEW_LOAD:
        draw_slots(app, true);
        break;
    case UI_VIEW_OVERWRITE: {
        char text[160];
        snprintf(text, sizeof(text), tx(ITX_CONFIRM_OVERWRITE),
                 ui->overwrite_slot + 1);
        draw_confirm(app, tx(ITX_TITLE_SAVE), text);
        break;
    }
    case UI_VIEW_NEW_CONFIRM:
        draw_confirm(app, tx(ITX_MENU_NEW_GAME), tx(ITX_CONFIRM_NEW_GAME));
        break;
    case UI_VIEW_CONTROLS: {
        const char *const LINES[] = {
            tx(ITX_CONTROLS_1), tx(ITX_CONTROLS_2), tx(ITX_CONTROLS_3),
            tx(ITX_CONTROLS_4), tx(ITX_CONTROLS_5), tx(ITX_CONTROLS_6),
            tx(ITX_CONTROLS_7),
        };
        draw_text_page(app, tx(ITX_TITLE_CONTROLS), LINES,
                       (int)(sizeof(LINES) / sizeof(LINES[0])), 420.0f);
        break;
    }
    case UI_VIEW_ABOUT:
        draw_about(app);
        break;
    default:
        break;
    }
}
