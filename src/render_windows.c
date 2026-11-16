/* The original's windows, laid out at the bounds TSW.exe gives its
 * controls in the 640x400 layout (docs/re/tools/form_layout.py runs
 * syokidata0 and the window openers on the form; 19-windows.md lists the
 * results).  Everything is drawn with the widgets in widgets.c. */

#include "render_windows.h"

#include "app.h"
#include "battle.h"
#include "items.h"
#include "render.h"
#include "state.h"
#include "widgets.h"

#include <stdio.h>
#include <string.h>

/* Application.Title (0x488e74): ShowMessage's caption. */
#define APP_TITLE "Tower of the Sorcerer Ver1.2"

static const char *list_text(const EventEngine *e, int index)
{
    return e->have_messages ? messages_get(&e->messages, index) : NULL;
}

static const char *ui_text(const EventEngine *e, int index, const char *dflt)
{
    const char *t = e->have_ui_texts ? messages_get(&e->ui_texts, index)
                                     : NULL;
    return t ? t : dflt;
}

static SDL_FRect rect(float x, float y, float w, float h)
{
    return (SDL_FRect){ x, y, w, h };
}

/* --------------------------------------------------------------------- */
/* RichEdit1 as zyouout places it: (129, 45) 382x200, clGrayText, white
 * Verdana at option byte 1's 12 pt.  Button1 sits 16 px inside its
 * bottom-right corner; handan puts Button7 (No) there and Button2 (Yes)
 * 16 px to its left. */

#define RICH_X 129.0f
#define RICH_Y 45.0f
#define RICH_W 382.0f
#define RICH_H 200.0f
#define BTN_W 75.0f
#define BTN_H 25.0f
#define BTN1_X (RICH_X + RICH_W - BTN_W - 16.0f)          /* 420 */
#define BTN1_Y (RICH_Y + RICH_H - BTN_H - 16.0f)          /* 204 */
#define BTN2_X (BTN1_X - BTN_W - 16.0f)                   /* 329 */

static void draw_rich_edit(App *app, SDL_FRect r, const char *text)
{
    SDL_Renderer *rr = app->render.renderer;
    SDL_FRect client = vcl_edit(rr, r, VCL_GRAY_TEXT);
    SDL_FRect pad = { client.x + 1.0f, client.y + 1.0f, client.w - 2.0f,
                      client.h - 2.0f };
    vcl_text_wrapped(rr, app->assets.vcl[VCL_FONT_16], text, pad, VCL_WINDOW);
}

static void draw_message_window(App *app)
{
    const EventEngine *e = &app->events.engine;
    SDL_Renderer *r = app->render.renderer;
    TTF_Font *btn = app->assets.vcl[VCL_FONT_12];
    const char *msg = event_engine_dialog(e);
    if (!msg)
        return;
    draw_rich_edit(app, rect(RICH_X, RICH_Y, RICH_W, RICH_H), msg);
    if (e->altar_open) {
        /* Button2Click mode 1: Button39..42, 350x32 from (145, 93), the
         * Edit8 font (-17); Button42 (cancel) has the focus first. */
        for (int i = 0; i < 4; i++)
            vcl_button(r, app->assets.vcl[VCL_FONT_17],
                       rect(RICH_X + 16.0f, RICH_Y + 48.0f + 32.0f * (float)i,
                            350.0f, 32.0f),
                       e->altar_options[i],
                       e->altar_focus == i ? VCL_FOCUSED : 0);
        return;
    }
    if (e->dialog_open && e->dialog_choice) {
        bool no = e->choice_focus_no;
        vcl_button(r, btn, rect(BTN2_X, BTN1_Y, BTN_W, BTN_H), "Yes",
                   no ? 0 : VCL_FOCUSED);
        vcl_button(r, btn, rect(BTN1_X, BTN1_Y, BTN_W, BTN_H), "No",
                   !e->choice_no_enabled ? VCL_DISABLED
                                         : no ? VCL_FOCUSED : 0);
    } else if (e->dialog_open) {
        vcl_button(r, btn, rect(BTN1_X, BTN1_Y, BTN_W, BTN_H),
                   event_engine_button_caption(e), VCL_FOCUSED);
    }
}

/* --------------------------------------------------------------------- */
/* The left panel's item grid after syokidata0: slot s at
 * (14 + 32 * (s % 3), 192 + 32 * (s / 3)). */

static float slot_x(int slot) { return 14.0f + 32.0f * (float)(slot % 3); }
static float slot_y(int slot) { return 192.0f + 32.0f * (float)(slot / 3); }

/* img8work .. img17work: Button37 ("Don't use", focused) and Button38
 * ("Use"), 64x16, under the item's picture, centred on it. */
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
    TTF_Font *f = app->assets.vcl[VCL_FONT_12];
    float x = slot_x(slot) - 16.0f, y = slot_y(slot) + 32.0f;
    bool no = e->item_confirm_focus_no;
    vcl_button(r, f, rect(x, y, 64.0f, 16.0f), "Don't use",
               no ? VCL_FOCUSED : 0);
    vcl_button(r, f, rect(x, y + 16.0f, 64.0f, 16.0f), "Use",
               no ? 0 : VCL_FOCUSED);
}

/* img4work: Button8 "UP", Button9 "DOWN", Button10 "OK" at (70, 224..256),
 * 48x16; the floor shown is the map itself. */
static void draw_fly_panel(App *app)
{
    const EventEngine *e = &app->events.engine;
    SDL_Renderer *r = app->render.renderer;
    TTF_Font *f = app->assets.vcl[VCL_FONT_12];
    vcl_button(r, f, rect(70.0f, 224.0f, 48.0f, 16.0f), "UP",
               e->fly_up_enabled ? 0 : VCL_DISABLED);
    vcl_button(r, f, rect(70.0f, 240.0f, 48.0f, 16.0f), "DOWN",
               e->fly_down_enabled ? 0 : VCL_DISABLED);
    vcl_button(r, f, rect(70.0f, 256.0f, 48.0f, 16.0f), "OK", VCL_FOCUSED);
}

/* img5work: Button11..34 ("1".."24", 16x16 from (14, 224), six per row)
 * and Button35 "OK" (78, 288) 32x16; the old man's words appear in
 * RichEdit1 without Button1. */
static void draw_wisdom_window(App *app)
{
    const EventEngine *e = &app->events.engine;
    SDL_Renderer *r = app->render.renderer;
    TTF_Font *f = app->assets.vcl[VCL_FONT_12];
    for (int k = 0; k < 24; k++) {
        char cap[4];
        snprintf(cap, sizeof(cap), "%d", k + 1);
        int flags = e->wisdom_enabled[k] ? 0 : VCL_DISABLED;
        if (e->wisdom_focus == k)
            flags |= VCL_FOCUSED;
        vcl_button(r, f, rect(14.0f + 16.0f * (float)(k % 6),
                              224.0f + 16.0f * (float)(k / 6), 16.0f, 16.0f),
                   cap, flags);
    }
    vcl_button(r, f, rect(78.0f, 288.0f, 32.0f, 16.0f), "OK",
               e->wisdom_focus == 24 ? VCL_FOCUSED : 0);
    const char *msg = event_engine_dialog(e);
    if (msg)
        draw_rich_edit(app, rect(RICH_X, RICH_Y, RICH_W, RICH_H), msg);
}

/* --------------------------------------------------------------------- */
/* MessageDlg / ShowMessage: a dialog centred on the playfield. */

static void draw_message_dialog(App *app, const char *caption,
                                const char *text, bool icon, bool yes_no,
                                bool focus_no)
{
    SDL_Renderer *r = app->render.renderer;
    TTF_Font *f = app->assets.vcl[VCL_FONT_12];
    TTF_Font *cap = app->assets.vcl[VCL_FONT_12B];
    const float margin = 12.0f, gap = 12.0f;
    float text_w = vcl_text_width(f, text);
    if (text_w > 320.0f)
        text_w = 320.0f;
    int lines = 1;
    if (vcl_text_width(f, text) > 320.0f)
        lines = (int)(vcl_text_width(f, text) / 320.0f) + 1;
    float line_h = (float)TTF_GetFontLineSkip(f);
    float text_h = line_h * (float)lines;
    float content_w = (icon ? 32.0f + gap : 0.0f) + text_w;
    float buttons_w = yes_no ? 2.0f * BTN_W + 6.0f : BTN_W;
    float cw = (content_w > buttons_w ? content_w : buttons_w) + 2.0f * margin;
    float body_h = icon && text_h < 32.0f ? 32.0f : text_h;
    float ch = margin + body_h + gap + BTN_H + margin;
    float w = cw + 6.0f, h = ch + 25.0f;
    SDL_FRect win = rect((float)(int)((640.0f - w) / 2.0f),
                         (float)(int)((400.0f - h) / 2.0f), w, h);
    SDL_FRect c = vcl_window(r, cap, win, caption);
    float tx = c.x + margin;
    if (icon) {
        vcl_info_icon(r, c.x + margin, c.y + margin, app->assets.vcl[VCL_FONT_16B]);
        tx += 32.0f + gap;
    }
    float ty = c.y + margin + (body_h - text_h) / 2.0f;
    vcl_text_wrapped(r, f, text, rect(tx, ty, text_w + 1.0f, text_h + 2.0f),
                     VCL_BLACK);
    float by = c.y + margin + body_h + gap;
    float bx = c.x + (c.w - buttons_w) / 2.0f;
    if (yes_no) {
        vcl_button(r, f, rect(bx, by, BTN_W, BTN_H), "Yes",
                   focus_no ? 0 : VCL_FOCUSED);
        vcl_button(r, f, rect(bx + BTN_W + 6.0f, by, BTN_W, BTN_H), "No",
                   focus_no ? VCL_FOCUSED : 0);
    } else {
        vcl_button(r, f, rect(bx, by, BTN_W, BTN_H), "OK", VCL_FOCUSED);
    }
}

/* --------------------------------------------------------------------- */
/* Form2 (the ranking) after its formactivate (0x434bc4): 480x444, centred
 * on the screen. */

static void draw_ranking(App *app)
{
    const EventEngine *e = &app->events.engine;
    SDL_Renderer *r = app->render.renderer;
    TTF_Font *f12 = app->assets.vcl[VCL_FONT_12];
    TTF_Font *f12b = app->assets.vcl[VCL_FONT_12B];
    SDL_FRect win = rect(80.0f, 18.0f, 480.0f, 444.0f);
    SDL_FRect c = vcl_window(r, f12b, win, "Ranking");
    float ox = c.x, oy = c.y;
    /* Memo61: no border, btnface, -16 bold, centred. */
    vcl_text_in(r, app->assets.vcl[VCL_FONT_16B], "RANKING of Top 10",
                rect(ox + 16.0f, oy + 8.0f, 442.0f, 28.0f), VCL_CENTER, 0.0f,
                VCL_BLACK);
    /* BitBtn1..5: 56x56, the area's floor under its glyph; the shown
     * area's button has the focus (FUN_00437f2c). */
    static const char *AREAS[5] = { "10F", "20F", "25F", "40F", "49F" };
    for (int i = 0; i < 5; i++) {
        SDL_FRect b = rect(ox + 49.0f + 80.0f * (float)i, oy + 44.0f, 56.0f,
                           56.0f);
        vcl_button(r, f12, b, "", e->ranking_area == i + 1 ? VCL_FOCUSED : 0);
        /* blGlyphTop: the 32x32 glyph and the caption centred together,
         * 4 px apart. */
        float th = (float)TTF_GetFontHeight(f12);
        float top = b.y + (b.h - (32.0f + 4.0f + th)) / 2.0f;
        SDL_Texture *glyph = app->assets.rank_glyph[i];
        if (glyph) {
            SDL_FRect g = { b.x + 12.0f, (float)(int)top, 32.0f, 32.0f };
            SDL_RenderTexture(r, glyph, NULL, &g);
        }
        vcl_text_in(r, f12, AREAS[i], rect(b.x, top + 36.0f, b.w, th),
                    VCL_CENTER, 0.0f, VCL_BLACK);
    }
    /* Labels (disabled: etched) and the ten rows of memos. */
    static const float COL_X[6] = { 16, 64, 127, 279, 349, 404 };
    static const float COL_W[6] = { 40, 55, 152, 70, 55, 55 };
    static const char *HEAD[6] = { "Rank", "Points", "Name", "Life",
                                   "Offense", "Defense" };
    static const int ALIGN[6] = { VCL_CENTER, VCL_RIGHT, VCL_LEFT,
                                  VCL_RIGHT, VCL_RIGHT, VCL_RIGHT };
    for (int col = 0; col < 6; col++)
        vcl_label_disabled(r, f12, HEAD[col],
                           rect(ox + COL_X[col], oy + 108.0f, COL_W[col],
                                16.0f), VCL_CENTER);
    for (int row = 0; row < RANKING_ROWS; row++) {
        const int32_t *v = &e->ranking.v[row * 6];
        char cells[6][RANKING_NAME_LEN + 8];
        snprintf(cells[0], sizeof(cells[0]), "%d", row + 1);
        snprintf(cells[1], sizeof(cells[1]), "%d", (int)v[0]);
        snprintf(cells[2], sizeof(cells[2]), "%s", e->ranking.names[row]);
        snprintf(cells[3], sizeof(cells[3]), "%d", (int)v[1]);
        snprintf(cells[4], sizeof(cells[4]), "%d", (int)v[2]);
        snprintf(cells[5], sizeof(cells[5]), "%d", (int)v[3]);
        /* The first row: clGrayText with red text; the rest white with
         * black (the DFM's colours). */
        bool first = row == 0;
        for (int col = 0; col < 6; col++) {
            SDL_FRect m = rect(ox + COL_X[col], oy + 126.0f + 25.0f * (float)row,
                               COL_W[col], 25.0f);
            SDL_FRect in = vcl_edit(r, m, first ? VCL_GRAY_TEXT : VCL_WINDOW);
            vcl_text_in(r, f12b, cells[col], in, ALIGN[col], 2.0f,
                        first ? VCL_RED : VCL_BLACK);
        }
    }
    vcl_button(r, f12, rect(ox + 128.0f, oy + 384.0f, 96.0f, 25.0f), "OK",
               0);
    vcl_button(r, f12, rect(ox + 256.0f, oy + 384.0f, 96.0f, 25.0f), "Clear", 0);

    if (e->ranking_name_entry) {
        /* InputQuery (Delphi 2): 270x~100 client, the prompt, an edit,
         * OK and Cancel. */
        char buf[RANKING_NAME_LEN * 4 + 8];
        snprintf(buf, sizeof(buf), "%s_", app->events.name_input);
        SDL_FRect q = rect(182.0f, 140.0f, 276.0f, 128.0f);
        SDL_FRect qc = vcl_window(r, f12b, q, ui_text(e, 6, "Input your name"));
        vcl_text(r, f12, ui_text(e, 7, "Please input within 18 words"),
                 qc.x + 12.0f, qc.y + 8.0f, VCL_BLACK);
        SDL_FRect ed = vcl_edit(r, rect(qc.x + 12.0f, qc.y + 28.0f, 246.0f,
                                        21.0f), VCL_WINDOW);
        vcl_text_in(r, f12, buf, ed, VCL_LEFT, 1.0f, VCL_BLACK);
        vcl_button(r, f12, rect(qc.x + 57.0f, qc.y + 62.0f, BTN_W, BTN_H),
                   "OK", VCL_FOCUSED);
        vcl_button(r, f12, rect(qc.x + 138.0f, qc.y + 62.0f, BTN_W, BTN_H),
                   "Cancel", 0);
    }
}

/* --------------------------------------------------------------------- */

void render_windows_draw(App *app)
{
    const EventEngine *e = &app->events.engine;
    if (e->ranking_open)
        draw_ranking(app);
    if (e->message_open) {
        draw_message_dialog(app, APP_TITLE, e->message_text, false, false,
                            false);
        return;
    }
    if (e->ranking_open)
        return;
    if (e->death_prompt) {
        /* MessageDlg(mtInformation, [mbYes, mbNo]). */
        draw_message_dialog(app, "Information",
                            e->death_prompt_text[0] ? e->death_prompt_text
                                                    : "Try again?",
                            true, true, e->choice_focus_no);
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
/* img6work / yousya: Panel1 over the map, RichEdit1 (141, 16) 358x56 with
 * ListBox2[0xb4], seven rows 40 px apart from y 80: the monster's picture
 * (141, 84), its name (Edit13, 181 114x20), obudameiji's damage (Edit27,
 * 295 204x20, red) and Life/Offense/Defense (Edit14, 181 318x20); OK
 * (Button36) at (435, 368). */

void render_hero_orb(App *app)
{
    const EventEngine *e = &app->events.engine;
    SDL_Renderer *r = app->render.renderer;
    TTF_Font *f13 = app->assets.vcl[VCL_FONT_13B];
    vcl_panel(r, rect(125.0f, 0.0f, 390.0f, 397.0f), VCL_GRAY_TEXT);
    const char *caption = list_text(e, 0xb4);
    draw_rich_edit(app, rect(141.0f, 16.0f, 358.0f, 56.0f),
                   caption ? caption : "");
    long long d = (long long)state_get(&app->game.state, ST_OPTION_A) + 1;
    for (int i = 0; i < 7; i++) {
        float y = 80.0f + 40.0f * (float)i;
        int index = i < e->hero_rows ? e->hero_row_index[i] : -1;
        render_panel_icon(app, index >= 0 ? event_engine_monster_picture(index)
                                          : -1, 141.0f, y + 4.0f);
        SDL_FRect name = vcl_edit(r, rect(181.0f, y, 114.0f, 20.0f),
                                  VCL_GRAY_TEXT);
        SDL_FRect dmg = vcl_edit(r, rect(295.0f, y, 204.0f, 20.0f),
                                 VCL_GRAY_TEXT);
        SDL_FRect stats = vcl_edit(r, rect(181.0f, y + 20.0f, 318.0f, 20.0f),
                                   VCL_GRAY_TEXT);
        if (index < 0 || index >= MONSTER_COUNT)
            continue;
        const MonsterDef *m = &MONSTER_TABLE[index];
        const char *nm = list_text(e, event_engine_monster_name_id(index));
        vcl_text_in(r, f13, nm ? nm : m->name, name, VCL_LEFT, 1.0f,
                    VCL_WINDOW);
        char buf[160];
        int damage = e->hero_row_damage[i];
        const char *label = list_text(e, damage >= 0 ? 0xb8 : 0xb9);
        if (damage >= 0)
            snprintf(buf, sizeof(buf), "%s %d", label ? label : "", damage);
        else
            snprintf(buf, sizeof(buf), "%s", label ? label : "");
        vcl_text_in(r, f13, buf, dmg, VCL_LEFT, 1.0f, VCL_RED);
        const char *l = list_text(e, 0xd6), *o = list_text(e, 0xd7),
                   *df = list_text(e, 0xd8);
        /* yousya's sixth row leaves out the space after "Offense". */
        snprintf(buf, sizeof(buf), "%s %lld %s%s%lld %s %lld", l ? l : "",
                 m->hp * d, o ? o : "", i == 5 ? "" : " ", m->attack * d,
                 df ? df : "", m->defense * d);
        vcl_text_in(r, f13, buf, stats, VCL_LEFT, 1.0f, VCL_WINDOW);
    }
    vcl_button(r, app->assets.vcl[VCL_FONT_12],
               rect(435.0f, 368.0f, 64.0f, 20.0f), "OK", VCL_FOCUSED);
}
