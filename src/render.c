#include "render.h"

#include "app.h"
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

    for (int ty = 0; ty < MAP_TILE_ROWS; ty++) {
        for (int tx = 0; tx < MAP_TILE_COLS; tx++) {
            uint8_t raw = map_tile(&app->map, app->floor, ty * MAP_TILE_COLS + tx);
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

static void draw_player_marker(App *app)
{
    SDL_Renderer *r = app->render.renderer;
    float x = (float)(TSW_MAP_X + app->player_x * TSW_TILE_SIZE);
    float y = (float)(TSW_MAP_Y + app->player_y * TSW_TILE_SIZE);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 170);
    SDL_FRect shadow = { x + 5.0f, y + 5.0f, 22.0f, 22.0f };
    SDL_RenderFillRect(r, &shadow);

    SDL_SetRenderDrawColor(r, 255, 225, 40, 255);
    SDL_FRect marker = { x + 7.0f, y + 7.0f, 18.0f, 18.0f };
    SDL_RenderFillRect(r, &marker);

    SDL_SetRenderDrawColor(r, 40, 30, 0, 255);
    SDL_FRect inner = { x + 11.0f, y + 11.0f, 10.0f, 10.0f };
    SDL_RenderFillRect(r, &inner);
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

static void draw_menu(App *app)
{
    SDL_Renderer *r = app->render.renderer;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    SDL_SetRenderDrawColor(r, 0, 0, 0, 185);
    SDL_FRect veil = { 0.0f, 0.0f, (float)TSW_CANVAS_W, (float)TSW_CANVAS_H };
    SDL_RenderFillRect(r, &veil);

    SDL_FRect panel = { 188.0f, 156.0f, 264.0f, 158.0f };
    SDL_SetRenderDrawColor(r, 34, 38, 52, 248);
    SDL_RenderFillRect(r, &panel);
    SDL_SetRenderDrawColor(r, 145, 160, 190, 255);
    SDL_RenderRect(r, &panel);

    SDL_Color title_color = { 240, 235, 205, 255 };
    SDL_Color item_color = { 210, 218, 230, 255 };
    SDL_Color selected_color = { 255, 238, 150, 255 };

    const char *title = "Menu";
    float title_x = panel.x + (panel.w - text_width(app->assets.font, title)) / 2.0f;
    draw_text(r, app->assets.font, title, title_x, panel.y + 14.0f, title_color);

    for (int i = 0; i < UI_MENU_COUNT; i++) {
        SDL_FRect row = { panel.x + 22.0f, panel.y + 52.0f + i * 40.0f,
                          panel.w - 44.0f, 30.0f };
        if (i == app->ui.menu_selected) {
            SDL_SetRenderDrawColor(r, 92, 102, 132, 255);
            SDL_RenderFillRect(r, &row);
        }
        const char *label = ui_menu_label(i);
        SDL_Color color = (i == app->ui.menu_selected) ? selected_color : item_color;
        float lx = row.x + (row.w - text_width(app->assets.font, label)) / 2.0f;
        draw_text(r, app->assets.font, label, lx, row.y + 6.0f, color);
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
    draw_player_marker(app);
    draw_console(app);
    if (app->ui.menu_open)
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
