#include "render.h"

#include "app.h"
#include "game.h"
#include "map.h"
#include "ui.h"

#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <stdio.h>

static void draw_text(SDL_Renderer *renderer, TTF_Font *font,
                      const char *text, float x, float y, SDL_Color color)
{
    if (!renderer || !font || !text || !*text)
        return;
    SDL_Surface *surface = TTF_RenderText_Blended(font, text, 0, color);
    if (!surface)
        return;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        SDL_FRect dst = { x, y, (float)surface->w, (float)surface->h };
        SDL_RenderTexture(renderer, texture, NULL, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_DestroySurface(surface);
}

static void draw_text_box_center(SDL_Renderer *renderer, TTF_Font *font,
                                 const char *text, float bx, float by,
                                 float bw, float bh, SDL_Color color)
{
    int w = 0, h = 0;
    if (!font || !text)
        return;
    if (!TTF_GetStringSize(font, text, 0, &w, &h))
        return;
    draw_text(renderer, font, text,
              bx + (bw - w) / 2.0f,
              by + (bh - h) / 2.0f,
              color);
}

static float text_width(TTF_Font *font, const char *text)
{
    int w = 0, h = 0;
    if (!font || !text)
        return 0.0f;
    if (!TTF_GetStringSize(font, text, 0, &w, &h))
        return 0.0f;
    return (float)w;
}

static void draw_map(App *app)
{
    if (!app || !app->assets.tiles)
        return;

    int floor = game_floor(&app->game);
    for (int ty = 0; ty < MAP_TILE_ROWS; ty++) {
        for (int tx = 0; tx < MAP_TILE_COLS; tx++) {
            uint8_t raw = map_tile(&app->game.map, floor,
                                   ty * MAP_TILE_COLS + tx);
            if (raw == 0)
                continue;
            int sprite = (int)raw - 1;
            if (sprite < 0 || sprite >= 11 * 20)
                continue;
            SDL_FRect src = {
                (float)((sprite % 11) * TSW_TILE_SIZE),
                (float)((sprite / 11) * TSW_TILE_SIZE),
                (float)TSW_TILE_SIZE,
                (float)TSW_TILE_SIZE
            };
            SDL_FRect dst = {
                (float)(TSW_MAP_X + tx * TSW_TILE_SIZE),
                (float)(TSW_MAP_Y + ty * TSW_TILE_SIZE),
                (float)TSW_TILE_SIZE,
                (float)TSW_TILE_SIZE
            };
            SDL_RenderTexture(app->render.renderer, app->assets.tiles, &src, &dst);
        }
    }
}

static void draw_player_sprite(App *app)
{
    if (!app->assets.player)
        return;

    int facing = (int)state_get(&app->game.state, ST_FACING);
    if (facing < 1 || facing > 4)
        facing = 1;

    float px = (float)game_x(&app->game);
    float py = (float)game_y(&app->game);
    if (app->step_active) {
        Uint64 now = app_now(app);
        float t = (float)(now - app->step_start_ms) / 90.0f;
        if (t >= 1.0f) {
            app->step_active = false;
        } else {
            px = app->step_from_x +
                 ((float)game_x(&app->game) - app->step_from_x) * t;
            py = app->step_from_y +
                 ((float)game_y(&app->game) - app->step_from_y) * t;
        }
    }

    SDL_FRect src = { (float)((facing - 1) * 32), 0.0f, 32.0f, 32.0f };
    SDL_FRect dst = {
        (float)TSW_MAP_X + px * TSW_TILE_SIZE,
        (float)TSW_MAP_Y + py * TSW_TILE_SIZE,
        (float)TSW_TILE_SIZE,
        (float)TSW_TILE_SIZE
    };
    SDL_RenderTexture(app->render.renderer, app->assets.player, &src, &dst);
}

static const char *equipment_name(int level)
{
    switch (level) {
    case 1: return "Iron";
    case 2: return "Silver";
    case 3: return "Knight";
    case 4: return "Holy";
    case 5: return "Sacred";
    default: return "-";
    }
}

static void draw_key_sprite(App *app, int sprite, float x, float y)
{
    if (!app->assets.keys || sprite < 0 || sprite > 3)
        return;
    SDL_FRect src = {
        (float)((sprite % 2) * 16),
        (float)((sprite / 2) * 16),
        16.0f, 16.0f
    };
    SDL_FRect dst = { x, y, 16.0f, 16.0f };
    SDL_RenderTexture(app->render.renderer, app->assets.keys, &src, &dst);
}

static void draw_key_grid(App *app, float x, float y)
{
    int red = (int)game_stat(&app->game, ST_KEY_RED);
    int blue = (int)game_stat(&app->game, ST_KEY_BLUE);
    int yellow = (int)game_stat(&app->game, ST_KEY_YELLOW);
    int cell = 0;

    for (int i = 0; i < red && cell < 24; i++, cell++)
        draw_key_sprite(app, 0, x + (cell % 6) * 16.0f, y + (cell / 6) * 16.0f);
    for (int i = 0; i < blue && cell < 24; i++, cell++)
        draw_key_sprite(app, 1, x + (cell % 6) * 16.0f, y + (cell / 6) * 16.0f);
    for (int i = 0; i < yellow && cell < 24; i++, cell++)
        draw_key_sprite(app, 2, x + (cell % 6) * 16.0f, y + (cell / 6) * 16.0f);
    for (; cell < 24; cell++)
        draw_key_sprite(app, 3, x + (cell % 6) * 16.0f, y + (cell / 6) * 16.0f);
}

static void draw_hud(App *app)
{
    SDL_Renderer *r = app->render.renderer;
    TTF_Font *font = app->assets.font_hud ? app->assets.font_hud
                                          : app->assets.font;
    SDL_Color white = { 255, 255, 255, 255 };
    char buf[96];

    /* Left panel: the original layout has a wide top box and four icon
     * rows.  Keep all text inside those boxes. */
    snprintf(buf, sizeof(buf), "FLOOR %d", game_floor(&app->game));
    draw_text_box_center(r, font, buf, 15.0f, 47.0f, 99.0f, 20.0f, white);

    snprintf(buf, sizeof(buf), "%d", (int)game_stat(&app->game, ST_LIFE));
    draw_text_box_center(r, font, buf, 42.0f, 80.0f, 72.0f, 16.0f, white);
    snprintf(buf, sizeof(buf), "%d", (int)game_stat(&app->game, ST_ATTACK));
    draw_text_box_center(r, font, buf, 42.0f, 104.0f, 72.0f, 16.0f, white);
    snprintf(buf, sizeof(buf), "%d", (int)game_stat(&app->game, ST_DEFENSE));
    draw_text_box_center(r, font, buf, 42.0f, 128.0f, 72.0f, 16.0f, white);
    snprintf(buf, sizeof(buf), "%d", (int)game_stat(&app->game, ST_GOLD));
    draw_text_box_center(r, font, buf, 42.0f, 152.0f, 72.0f, 16.0f, white);

    /* Right panel: weapon/shield name fields are the light boxes directly
     * below their baked labels.  Key counts go in the dark middle box. */
    snprintf(buf, sizeof(buf), "%s",
             equipment_name((int)game_stat(&app->game, ST_WEAPON_LEVEL)));
    draw_text_box_center(r, font, buf, 526.0f, 66.0f, 64.0f, 16.0f, white);
    snprintf(buf, sizeof(buf), "%s",
             equipment_name((int)game_stat(&app->game, ST_SHIELD_LEVEL)));
    draw_text_box_center(r, font, buf, 526.0f, 114.0f, 64.0f, 16.0f, white);

    draw_key_grid(app, 528.0f, 144.0f);
}

static void draw_console(App *app)
{
    SDL_Renderer *r = app->render.renderer;
    SDL_SetRenderDrawColor(r, 12, 14, 22, 255);
    SDL_FRect console = { 0.0f, (float)TSW_PLAYFIELD_H,
                          (float)TSW_CANVAS_W, (float)TSW_CONSOLE_H };
    SDL_RenderFillRect(r, &console);

    SDL_SetRenderDrawColor(r, 90, 100, 125, 255);
    SDL_RenderLine(r, 0.0f, (float)TSW_PLAYFIELD_H,
                   (float)TSW_CANVAS_W, (float)TSW_PLAYFIELD_H);

    SDL_Color color = { 215, 225, 240, 255 };
    float y = (float)TSW_PLAYFIELD_H + 5.0f;
    for (int i = 0; i < app->log.count; i++) {
        draw_text(r, app->assets.font, log_line(&app->log, i), 8.0f, y, color);
        y += 18.0f;
    }
}

static void draw_modal_panel(App *app, float w, float h,
                             const char *title)
{
    SDL_Renderer *r = app->render.renderer;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    SDL_SetRenderDrawColor(r, 0, 0, 0, 185);
    SDL_FRect veil = { 0.0f, 0.0f, (float)TSW_CANVAS_W, (float)TSW_CANVAS_H };
    SDL_RenderFillRect(r, &veil);

    SDL_FRect panel = {
        ((float)TSW_CANVAS_W - w) / 2.0f,
        ((float)TSW_CANVAS_H - h) / 2.0f,
        w, h
    };
    SDL_SetRenderDrawColor(r, 34, 38, 52, 248);
    SDL_RenderFillRect(r, &panel);
    SDL_SetRenderDrawColor(r, 145, 160, 190, 255);
    SDL_RenderRect(r, &panel);

    SDL_Color title_color = { 240, 235, 205, 255 };
    float title_x = panel.x + (panel.w - text_width(app->assets.font, title)) / 2.0f;
    draw_text(r, app->assets.font, title, title_x, panel.y + 12.0f, title_color);
}

static void draw_menu_rows(App *app, const char *const *labels, int count,
                           int selected, float start_y, float row_w)
{
    SDL_Renderer *r = app->render.renderer;
    SDL_Color item_color = { 210, 218, 230, 255 };
    SDL_Color selected_color = { 255, 238, 150, 255 };
    float panel_x = ((float)TSW_CANVAS_W - row_w) / 2.0f;

    for (int i = 0; i < count; i++) {
        SDL_FRect row = { panel_x + 18.0f, start_y + i * 32.0f,
                          row_w - 36.0f, 28.0f };
        if (i == selected) {
            SDL_SetRenderDrawColor(r, 92, 102, 132, 255);
            SDL_RenderFillRect(r, &row);
        }
        SDL_Color color = (i == selected) ? selected_color : item_color;
        draw_text(r, app->assets.font, labels[i],
                  row.x + 10.0f, row.y + 5.0f, color);
    }
}

static void draw_menu(App *app)
{
    const char *labels[UI_SLOT_COUNT];
    char slot_buf[UI_SLOT_COUNT][32];
    char extra[UI_SLOT_COUNT][16];
    int count = 0;
    int selected = 0;

    if (app->ui.view == UI_VIEW_MENU) {
        count = UI_MENU_COUNT;
        selected = app->ui.menu_selected;
        for (int i = 0; i < count; i++)
            labels[i] = ui_menu_label(i);
        draw_modal_panel(app, 300.0f, 250.0f, ui_view_title(app->ui.view));
        draw_menu_rows(app, labels, count, selected,
                       ((float)TSW_CANVAS_H - 250.0f) / 2.0f + 50.0f, 300.0f);
        return;
    }

    if (app->ui.view == UI_VIEW_SAVE || app->ui.view == UI_VIEW_LOAD) {
        count = UI_SLOT_COUNT;
        selected = app->ui.slot_selected;
        for (int i = 0; i < count; i++) {
            snprintf(slot_buf[i], sizeof(slot_buf[i]), "Slot %d", i + 1);
            labels[i] = slot_buf[i];
            extra[i][0] = '\0';
            if (app_save_slot_exists(app, i + 1))
                snprintf(extra[i], sizeof(extra[i]), "exists");
        }
        draw_modal_panel(app, 320.0f, 330.0f, ui_view_title(app->ui.view));
        float panel_y = ((float)TSW_CANVAS_H - 330.0f) / 2.0f;
        draw_menu_rows(app, labels, count, selected, panel_y + 48.0f, 320.0f);
        SDL_Color note = { 150, 160, 180, 255 };
        for (int i = 0; i < count; i++) {
            if (extra[i][0])
                draw_text(app->render.renderer, app->assets.font, extra[i],
                          430.0f, panel_y + 54.0f + i * 32.0f, note);
        }
        draw_text(app->render.renderer, app->assets.font,
                  "Enter select   Esc back", 238.0f, panel_y + 304.0f, note);
        return;
    }

    if (app->ui.view == UI_VIEW_NEW_CONFIRM) {
        SDL_Color text = { 210, 218, 230, 255 };
        draw_modal_panel(app, 380.0f, 160.0f, ui_view_title(app->ui.view));
        draw_text(app->render.renderer, app->assets.font,
                  "Start a new game? Current progress will be lost.",
                  148.0f, 226.0f, text);
        draw_text(app->render.renderer, app->assets.font,
                  "Enter start   Esc cancel",
                  222.0f, 254.0f, text);
        return;
    }

    if (app->ui.view == UI_VIEW_OVERWRITE) {
        char line[64];
        draw_modal_panel(app, 320.0f, 150.0f, ui_view_title(app->ui.view));
        snprintf(line, sizeof(line), "Slot %d already exists.",
                 app->ui.overwrite_slot + 1);
        SDL_Color text = { 210, 218, 230, 255 };
        draw_text(app->render.renderer, app->assets.font, line,
                  242.0f, 226.0f, text);
        draw_text(app->render.renderer, app->assets.font,
                  "Enter overwrite   Esc cancel",
                  202.0f, 254.0f, text);
    }
}

bool render_init(Renderer *renderer, const char *title,
                 char *err, size_t errsz)
{
    if (!renderer) {
        if (err && errsz) snprintf(err, errsz, "null renderer");
        return false;
    }
    renderer->window = SDL_CreateWindow(title, TSW_CANVAS_W, TSW_CANVAS_H,
                                        SDL_WINDOW_RESIZABLE);
    if (!renderer->window) {
        if (err && errsz)
            snprintf(err, errsz, "cannot create window: %s", SDL_GetError());
        return false;
    }
    renderer->renderer = SDL_CreateRenderer(renderer->window, NULL);
    if (!renderer->renderer) {
        if (err && errsz)
            snprintf(err, errsz, "cannot create renderer: %s", SDL_GetError());
        SDL_DestroyWindow(renderer->window);
        renderer->window = NULL;
        return false;
    }
    SDL_SetRenderVSync(renderer->renderer, 1);
    SDL_SetRenderLogicalPresentation(renderer->renderer, TSW_CANVAS_W,
                                     TSW_CANVAS_H,
                                     SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    return true;
}

void render_shutdown(Renderer *renderer)
{
    if (!renderer) return;
    if (renderer->renderer) SDL_DestroyRenderer(renderer->renderer);
    if (renderer->window) SDL_DestroyWindow(renderer->window);
    renderer->renderer = NULL;
    renderer->window = NULL;
}

void render_draw(App *app)
{
    if (!app || !app->render.renderer)
        return;

    SDL_Renderer *r = app->render.renderer;
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);

    SDL_FRect left = { 0.0f, 0.0f, 125.0f, 400.0f };
    SDL_FRect center = { 125.0f, 0.0f, 390.0f, 400.0f };
    SDL_FRect right = { 515.0f, 0.0f, 125.0f, 400.0f };
    if (app->assets.left) SDL_RenderTexture(r, app->assets.left, NULL, &left);
    if (app->assets.center) SDL_RenderTexture(r, app->assets.center, NULL, &center);
    if (app->assets.right) SDL_RenderTexture(r, app->assets.right, NULL, &right);

    draw_map(app);
    draw_player_sprite(app);
    draw_hud(app);
    draw_console(app);
    if (app->ui.view != UI_VIEW_GAME)
        draw_menu(app);
}

void render_present(Renderer *renderer)
{
    if (renderer && renderer->renderer)
        SDL_RenderPresent(renderer->renderer);
}

void render_frame(App *app)
{
    render_draw(app);
    render_present(&app->render);
}

bool render_save_png(Renderer *renderer, const char *path,
                     char *err, size_t errsz)
{
    if (!renderer || !renderer->renderer || !path) {
        if (err && errsz) snprintf(err, errsz, "screenshot: missing renderer/path");
        return false;
    }
    SDL_Surface *surface = SDL_RenderReadPixels(renderer->renderer, NULL);
    if (!surface) {
        if (err && errsz)
            snprintf(err, errsz, "screenshot read failed: %s", SDL_GetError());
        return false;
    }
    bool ok = IMG_SavePNG(surface, path);
    if (!ok && err && errsz)
        snprintf(err, errsz, "screenshot save failed: %s", SDL_GetError());
    SDL_DestroySurface(surface);
    return ok;
}
