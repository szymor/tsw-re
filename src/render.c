#include "render.h"

#include "app.h"
#include "game.h"
#include "intro.h"
#include "items.h"
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

static void draw_text_wrapped(SDL_Renderer *renderer, TTF_Font *font,
                              const char *text, float x, float y,
                              float width, SDL_Color color)
{
    if (!renderer || !font || !text || !*text)
        return;
    SDL_Surface *surface = TTF_RenderText_Blended_Wrapped(font, text, 0,
                                                          color, (int)width);
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

static void draw_atlas_sprite(SDL_Renderer *r, SDL_Texture *atlas,
                              int sprite, float x, float y, float size)
{
    if (!r || !atlas || sprite < 0 || sprite >= 11 * 20)
        return;
    SDL_FRect src = {
        (float)((sprite % 11) * TSW_TILE_SIZE),
        (float)((sprite / 11) * TSW_TILE_SIZE),
        (float)TSW_TILE_SIZE, (float)TSW_TILE_SIZE
    };
    SDL_FRect dst = { x, y, size, size };
    SDL_RenderTexture(r, atlas, &src, &dst);
}

/* The 1-based tile `value` as the map buffer on the form holds it: frame A
 * as is, frame B plus imagework's offset (Timer1, event_engine.h). */
static int shown_tile(const App *app, int value)
{
    if (app->events.engine.shown_buffer != 1 || value <= 0)
        return value;
    return value + event_engine_anim_offset(
        value, state_get(&app->game.state, 0x1d));
}

/* The hero's frame in Player.png: (facing - 1) * 3 + pose; a 4-frame sheet
 * from an older converter has only the walking pose B. */
static SDL_FRect hero_src(App *app, int facing, int pose)
{
    if (facing < 1 || facing > 4)
        facing = 1;
    float w = 0.0f, h = 0.0f;
    SDL_GetTextureSize(app->assets.player, &w, &h);
    int frame = w >= 12 * 32 ? (facing - 1) * 3 + pose : facing - 1;
    return (SDL_FRect){ (float)(frame * 32), 0.0f, 32.0f, 32.0f };
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
            int sprite = shown_tile(app, raw) - 1;
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

/* One 32x32 tile composed like kasanegaki (0x44439c) from 1-based Chtable
 * tiles: R = (B | ~M) & (S | M) per colour channel. */
static void draw_composed_tile(App *app, int background, int sprite,
                               int mask, float x, float y)
{
    SDL_Surface *atlas = app->assets.tiles_surface;
    const int n = 11 * 20;
    int b = background - 1, s = sprite - 1, m = mask - 1;
    if (!atlas || b < 0 || b >= n || s < 0 || s >= n || m < 0 || m >= n)
        return;
    SDL_Surface *tile = SDL_CreateSurface(TSW_TILE_SIZE, TSW_TILE_SIZE,
                                          SDL_PIXELFORMAT_RGBA32);
    if (!tile)
        return;
    for (int py = 0; py < TSW_TILE_SIZE; py++) {
        Uint8 *dst = (Uint8 *)tile->pixels + py * tile->pitch;
        const Uint8 *row[3];
        int idx[3] = { b, s, m };
        for (int k = 0; k < 3; k++)
            row[k] = (const Uint8 *)atlas->pixels +
                     ((idx[k] / 11) * TSW_TILE_SIZE + py) * atlas->pitch +
                     (idx[k] % 11) * TSW_TILE_SIZE * 4;
        for (int px = 0; px < TSW_TILE_SIZE; px++) {
            for (int c = 0; c < 3; c++) {
                Uint8 bv = row[0][px * 4 + c];
                Uint8 sv = row[1][px * 4 + c];
                Uint8 mv = row[2][px * 4 + c];
                dst[px * 4 + c] = (Uint8)((bv | (Uint8)~mv) & (sv | mv));
            }
            dst[px * 4 + 3] = 255;
        }
    }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(app->render.renderer, tile);
    SDL_DestroySurface(tile);
    if (!tex)
        return;
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    SDL_FRect dst = { x, y, (float)TSW_TILE_SIZE, (float)TSW_TILE_SIZE };
    SDL_RenderTexture(app->render.renderer, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
}

static Uint64 render_now(const App *app)
{
    return app->clock_fn ? app->clock_fn() : SDL_GetTicks();
}

static void draw_tile_part(App *app, SDL_Texture *atlas, int tile,
                           float sx, float sy, float w, float h,
                           float dx, float dy)
{
    int s = tile - 1;
    if (!atlas || s < 0 || s >= 11 * 20 || w <= 0 || h <= 0)
        return;
    SDL_FRect src = { (float)((s % 11) * TSW_TILE_SIZE) + sx,
                      (float)((s / 11) * TSW_TILE_SIZE) + sy, w, h };
    SDL_FRect dst = { dx, dy, w, h };
    SDL_RenderTexture(app->render.renderer, atlas, &src, &dst);
}

/* Door frames (op 0x0d, dooropen 0x4770e0): the base tile, then the
 * CopyRects from the door tile, at the door's progress. */
static void draw_engine_doors(App *app)
{
    int floor = game_floor(&app->game);
    for (int i = 0; i < EVENT_ENGINE_MAX_ACTORS; i++) {
        const EventDoorAnim *d = &app->events.engine.doors[i];
        EventDoorFrame f;
        if (!d->active || d->floor != floor || d->map_index < 0 ||
            d->map_index >= MAP_TILES ||
            !event_engine_door_frame(d->kind, d->progress, &f))
            continue;
        float x = (float)TSW_MAP_X + (d->map_index % MAP_TILE_COLS) * TSW_TILE_SIZE;
        float y = (float)TSW_MAP_Y + (d->map_index / MAP_TILE_COLS) * TSW_TILE_SIZE;
        draw_tile_part(app, app->assets.tiles, f.base_tile, 0, 0,
                       TSW_TILE_SIZE, TSW_TILE_SIZE, x, y);
        SDL_Texture *src = f.masked && app->assets.tiles_masked
            ? app->assets.tiles_masked : app->assets.tiles;
        for (int k = 0; k < f.nblits; k++) {
            const EventDoorBlit *b = &f.blits[k];
            draw_tile_part(app, src, f.src_tile, (float)b->sx, (float)b->sy,
                           (float)b->sw, (float)b->sh,
                           x + b->dx, y + b->dy);
        }
    }
}

/* Monster moves (op 0x0c, monidou 0x480384): floor under both cells, the
 * sprite (through its Chtableb mask) 8, 16, 24 then 32 px along. */
static void draw_engine_moves(App *app)
{
    int floor = game_floor(&app->game);
    Uint64 now = render_now(app);
    for (int i = 0; i < EVENT_ENGINE_MAX_MOVES; i++) {
        const EventMoveAnim *m = &app->events.engine.moves[i];
        if (!m->active || m->floor != floor || now >= m->start + m->duration ||
            m->from < 0 || m->from >= MAP_TILES || m->to < 0 || m->to >= MAP_TILES)
            continue;
        if (map_tile(&app->game.map, floor, m->to) != m->raw)
            continue;                   /* superseded by a later write */
        Uint64 dt = now > m->start ? now - m->start : 0;
        int step = 1 + (int)(dt * 4 / (m->duration ? m->duration : 1));
        if (step > 4) step = 4;
        int fx = m->from % MAP_TILE_COLS, fy = m->from / MAP_TILE_COLS;
        int tx = m->to % MAP_TILE_COLS, ty = m->to / MAP_TILE_COLS;
        float ox = (float)TSW_MAP_X, oy = (float)TSW_MAP_Y;
        draw_tile_part(app, app->assets.tiles, 6, 0, 0, TSW_TILE_SIZE,
                       TSW_TILE_SIZE, ox + tx * TSW_TILE_SIZE, oy + ty * TSW_TILE_SIZE);
        if (map_tile(&app->game.map, floor, m->from) == 0x06)
            draw_tile_part(app, app->assets.tiles, 6, 0, 0, TSW_TILE_SIZE,
                           TSW_TILE_SIZE, ox + fx * TSW_TILE_SIZE,
                           oy + fy * TSW_TILE_SIZE);
        float px = ox + fx * TSW_TILE_SIZE + (tx - fx) * step * 8.0f;
        float py = oy + fy * TSW_TILE_SIZE + (ty - fy) * step * 8.0f;
        SDL_Texture *atlas = app->assets.tiles_masked
            ? app->assets.tiles_masked : app->assets.tiles;
        draw_tile_part(app, atlas, shown_tile(app, m->raw), 0, 0, TSW_TILE_SIZE,
                       TSW_TILE_SIZE, px, py);
    }
}

/* banpaiawork (0x47c978), phase 0: the 3x3 floor block at cells (4,4), the
 * eight bats (sprite 0x53) moving `s` px in from the corners and edge
 * centres, then the vampire (0x5f) in the centre through the dissolve mask
 * 0xa6 + s/2.  1-based Chtable tiles. */
static void draw_engine_vampire(App *app)
{
    const EventVampire *v = &app->events.engine.vampire;
    if (!v->active || v->floor != game_floor(&app->game) || !app->assets.tiles)
        return;
    const float T = (float)TSW_TILE_SIZE;
    float x0 = (float)TSW_MAP_X + 4 * T, y0 = (float)TSW_MAP_Y + 4 * T;
    for (int i = 0; i < 9; i++)
        draw_tile_part(app, app->assets.tiles, 6, 0, 0, T, T,
                       x0 + (i % 3) * T, y0 + (i / 3) * T);
    float s = (float)(v->stage < 0 ? 0 : v->stage > 32 ? 32 : v->stage);
    const float bats[8][2] = {
        { s, s }, { s, 2 * T - s }, { 2 * T - s, s }, { 2 * T - s, 2 * T - s },
        { T, s }, { T, 2 * T - s }, { s, T }, { 2 * T - s, T },
    };
    SDL_Texture *sprites = app->assets.tiles_masked ? app->assets.tiles_masked
                                                    : app->assets.tiles;
    for (int i = 0; i < 8; i++)
        draw_tile_part(app, sprites, 0x53, 0, 0, T, T,
                       x0 + bats[i][0], y0 + bats[i][1]);
    draw_composed_tile(app, 6, 0x5f, 0xa6 + (int)s / 2, x0 + T, y0 + T);
}

static void draw_event_actors(App *app)
{
    if (!app || !app->assets.tiles)
        return;
    /* Actors animated by the command VM (op 0x09). */
    int floor = game_floor(&app->game);
    for (int i = 0; i < EVENT_ENGINE_MAX_ACTORS; i++) {
        const EventVmActor *va = &app->events.engine.actors[i];
        if (!va->active || va->floor != floor ||
            va->map_index < 0 || va->map_index >= MAP_TILES)
            continue;
        /* Only while the form shows it (Timer1's hero redraw takes it out
         * of one buffer per tick); frame B animates background and sprite
         * like the map. */
        if (!va->on_screen)
            continue;
        draw_composed_tile(app, shown_tile(app, va->background),
                           shown_tile(app, va->sprite), va->mask,
                           (float)TSW_MAP_X +
                               (va->map_index % MAP_TILE_COLS) * TSW_TILE_SIZE,
                           (float)TSW_MAP_Y +
                               (va->map_index / MAP_TILE_COLS) * TSW_TILE_SIZE);
    }
}

static SDL_Texture *cutscene_texture(App *app, int image)
{
    switch (image) {
    case 1: return app->assets.open0;
    case 2: return app->assets.open1;
    case 3: return app->assets.open2;
    default: return NULL;
    }
}

/* Sub-dispatch 0xf (0x4539be) copies the form into 0x48c528: keep what is
 * on screen now (the playfield, 640x400) as the dissolve's backdrop. */
static void capture_screen(App *app)
{
    Renderer *rd = &app->render;
    SDL_Surface *shot = SDL_RenderReadPixels(rd->renderer, NULL);
    if (!shot)
        return;
    SDL_Surface *rgba = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(shot);
    if (!rgba)
        return;
    if (rgba->w != TSW_CANVAS_W || rgba->h != TSW_CANVAS_H) {
        SDL_Surface *scaled = SDL_ScaleSurface(rgba, TSW_CANVAS_W,
                                               TSW_CANVAS_H,
                                               SDL_SCALEMODE_NEAREST);
        SDL_DestroySurface(rgba);
        if (!scaled)
            return;
        rgba = scaled;
    }
    if (rd->capture)
        SDL_DestroySurface(rd->capture);
    rd->capture = rgba;
}

/* kasanegaki over the form, as openning9 phases 0/2 draw a frame:
 * R = (B | ~M) & (S | M) per channel, M the mask tile repeated every 32
 * px (event_engine.h).  NULL if a source is missing. */
static SDL_Texture *compose_dissolve(App *app, const SDL_Surface *s,
                                     const SDL_Surface *b, int mask)
{
    const SDL_Surface *atlas = app->assets.tiles_surface;
    int m = mask - 1;
    if (!s || !b || !atlas || m < 0 || m >= 11 * 20 ||
        s->w < 640 || s->h < 400 || b->w < 640 || b->h < 400)
        return NULL;
    SDL_Surface *out = SDL_CreateSurface(640, 400, SDL_PIXELFORMAT_RGBA32);
    if (!out)
        return NULL;
    int mx = (m % 11) * TSW_TILE_SIZE, my = (m / 11) * TSW_TILE_SIZE;
    for (int y = 0; y < 400; y++) {
        const Uint8 *sr = (const Uint8 *)s->pixels + y * s->pitch;
        const Uint8 *br = (const Uint8 *)b->pixels + y * b->pitch;
        const Uint8 *mr = (const Uint8 *)atlas->pixels +
                          (my + y % TSW_TILE_SIZE) * atlas->pitch + mx * 4;
        Uint8 *d = (Uint8 *)out->pixels + y * out->pitch;
        for (int x = 0; x < 640; x++) {
            const Uint8 *mp = mr + (x % TSW_TILE_SIZE) * 4;
            for (int c = 0; c < 3; c++)
                d[x * 4 + c] = (Uint8)((br[x * 4 + c] | (Uint8)~mp[c]) &
                                       (sr[x * 4 + c] | mp[c]));
            d[x * 4 + 3] = 255;
        }
    }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(app->render.renderer, out);
    SDL_DestroySurface(out);
    if (tex)
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    return tex;
}

/* The floor-50 ending's pictures, openning9 phases 4..7 (event_engine.h).
 * A missing picture leaves the playfield black. */
static void draw_engine_ending(App *app, const EventCutscene *c)
{
    SDL_Renderer *r = app->render.renderer;
    SDL_FRect full = { 0.0f, 0.0f, 640.0f, 400.0f };
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderFillRect(r, &full);
    switch (c->ending_phase) {
    case 6: {
        int pic = event_engine_ending_picture(c->ending_frame);
        SDL_Texture *t = pic == 0 ? app->assets.open0
                       : pic >= 5 && pic <= 20 ? app->assets.end[pic - 5]
                       : NULL;
        if (t)
            SDL_RenderTexture(r, t, NULL, &full);
        break;
    }
    case 4: {
        int rc[4];
        event_engine_ending_shrink(c->ending_frame, rc);
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderFillRect(r, &full);
        SDL_FRect dst = { (float)rc[0], (float)rc[1],
                          (float)(rc[2] - rc[0]), (float)(rc[3] - rc[1]) };
        if (app->assets.end[15] && dst.w > 0 && dst.h > 0)
            SDL_RenderTexture(r, app->assets.end[15], NULL, &dst);
        break;
    }
    case 5: {
        if (app->assets.end26)
            SDL_RenderTexture(r, app->assets.end26, NULL, &full);
        int rows = c->ending_frame > 399 ? 400 : c->ending_frame;
        if (app->assets.image21 && rows > 0) {
            SDL_FRect part = { 0.0f, 0.0f, 640.0f, (float)rows };
            SDL_RenderTexture(r, app->assets.image21, &part, &part);
        }
        break;
    }
    case 7:
        if (app->assets.image33)
            SDL_RenderTexture(r, app->assets.image33, NULL, &full);
        break;
    default:
        break;
    }
}

/* Phase 8: Panel2 (455,358 185x40, a raised TPanel) with Image34 (the
 * "Go to the back side" picture, stretched into 8,8 169x25); a click
 * (Enter/Space in the remake) is image34click. */
static void draw_ending_button(App *app)
{
    SDL_Renderer *r = app->render.renderer;
    SDL_FRect panel = { 455.0f, 358.0f, 185.0f, 40.0f };
    SDL_SetRenderDrawColor(r, 192, 192, 192, 255);
    SDL_RenderFillRect(r, &panel);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderLine(r, 455.0f, 358.0f, 639.0f, 358.0f);
    SDL_RenderLine(r, 455.0f, 358.0f, 455.0f, 397.0f);
    SDL_SetRenderDrawColor(r, 128, 128, 128, 255);
    SDL_RenderLine(r, 455.0f, 397.0f, 639.0f, 397.0f);
    SDL_RenderLine(r, 639.0f, 358.0f, 639.0f, 397.0f);
    SDL_FRect img = { 463.0f, 366.0f, 169.0f, 25.0f };
    if (app->assets.image34)
        SDL_RenderTexture(r, app->assets.image34, NULL, &img);
}

/* openning9 picture over the 640x400 playfield (event_engine.h). */
static void draw_engine_cutscene(App *app)
{
    const EventEngine *e = &app->events.engine;
    const EventCutscene *c = &e->cutscene;
    Renderer *rd = &app->render;
    if (e->screen_captures != rd->capture_seq) {
        if (e->screen_captures > 0)
            capture_screen(app);
        rd->capture_seq = e->screen_captures;
    }
    if (c->ending_phase) {
        draw_engine_ending(app, c);
        if (e->ending_wait)
            draw_ending_button(app);
        return;
    }
    SDL_Texture *base = cutscene_texture(app, c->image);
    if (!c->image)
        return;
    SDL_FRect full = { 0.0f, 0.0f, 640.0f, 400.0f };
    if (c->dissolve > 0 && c->image >= 1 && c->image <= 3) {
        bool captured = e->screen_captures > 0 && rd->capture;
        int key = (c->image << 16) | (c->dissolve << 4) | (captured ? 1 : 0);
        if (!rd->dissolve_tex || rd->dissolve_key != key) {
            if (rd->dissolve_tex)
                SDL_DestroyTexture(rd->dissolve_tex);
            rd->dissolve_tex = compose_dissolve(
                app, app->assets.open_surface[c->image - 1],
                captured ? rd->capture : app->assets.panels_surface,
                c->dissolve);
            rd->dissolve_key = key;
        }
        if (rd->dissolve_tex) {
            SDL_RenderTexture(rd->renderer, rd->dissolve_tex, NULL, &full);
            return;
        }
    }
    if (base) {
        SDL_RenderTexture(app->render.renderer, base, NULL, &full);
    } else {
        SDL_SetRenderDrawColor(app->render.renderer, 0, 0, 0, 255);
        SDL_RenderFillRect(app->render.renderer, &full);
    }
    SDL_Texture *over = cutscene_texture(app, c->overlay);
    if (over && c->reveal > 0) {
        float rows = (float)c->reveal;
        SDL_FRect src = { 0.0f, 0.0f, 640.0f, rows };
        SDL_FRect dst = { 0.0f, 0.0f, 640.0f, rows };
        SDL_RenderTexture(app->render.renderer, over, &src, &dst);
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

    /* The pose the shown map buffer holds (Timer1, event_engine.h). */
    const EventEngine *e = &app->events.engine;
    SDL_FRect src = hero_src(app, facing, e->hero_shown_pose);
    SDL_FRect dst = {
        (float)TSW_MAP_X + px * TSW_TILE_SIZE,
        (float)TSW_MAP_Y + py * TSW_TILE_SIZE,
        (float)TSW_TILE_SIZE,
        (float)TSW_TILE_SIZE
    };
    SDL_RenderTexture(app->render.renderer, app->assets.player, &src, &dst);
}

/* Image1 (the center panel, 125,0 390x400, stretched) with the picture
 * sub-dispatch 7 loaded: title05.bmp or title05R.bmp over the map. */
static void draw_center_picture(App *app)
{
    int pic = app->events.engine.center_picture;
    SDL_Texture *t = pic == 1 ? app->assets.title05
                   : pic == 2 ? app->assets.title05r : NULL;
    if (!t)
        return;
    SDL_FRect dst = { 125.0f, 0.0f, 390.0f, 400.0f };
    SDL_RenderTexture(app->render.renderer, t, NULL, &dst);
}

/* The floor-change wipe over the map: cells not covered yet still show
 * the floor that was left (and the player there), covered cells the
 * ImageList tile 0x9e; the rest already shows the new floor. */
static void draw_engine_wipe(App *app)
{
    const EventWipe *w = &app->events.engine.wipe;
    if (!w->active || !app->assets.tiles)
        return;
    for (int c = 0; c < 121; c++) {
        float x = (float)(TSW_MAP_X + (c % MAP_TILE_COLS) * TSW_TILE_SIZE);
        float y = (float)(TSW_MAP_Y + (c / MAP_TILE_COLS) * TSW_TILE_SIZE);
        if (w->cell[c] == 1) {
            draw_atlas_sprite(app->render.renderer, app->assets.tiles, 0x9e,
                              x, y, (float)TSW_TILE_SIZE);
        } else if (w->cell[c] == 0) {
            int raw = w->old_tiles[c];
            if (raw > 0)
                draw_atlas_sprite(app->render.renderer, app->assets.tiles,
                                  shown_tile(app, raw) - 1, x, y,
                                  (float)TSW_TILE_SIZE);
            if (c == w->old_player && app->assets.player) {
                SDL_FRect src = hero_src(app, w->old_facing, EVENT_HERO_STAND);
                SDL_FRect dst = { x, y, (float)TSW_TILE_SIZE, (float)TSW_TILE_SIZE };
                SDL_RenderTexture(app->render.renderer, app->assets.player,
                                  &src, &dst);
            }
        }
    }
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

/* One 32x32 panel image as itemdisp (0x44ccd8) paints it: the panel tile
 * (ImageList index 0xa2), then the sprite through its Chtableb mask when
 * there is one (sprite < 0: background only).  Indices are 0-based. */
static void draw_panel_icon(App *app, int sprite, float x, float y)
{
    SDL_Renderer *r = app->render.renderer;
    draw_atlas_sprite(r, app->assets.tiles, 0xa2, x, y, (float)TSW_TILE_SIZE);
    if (sprite >= 0)
        draw_atlas_sprite(r, app->assets.tiles_masked ? app->assets.tiles_masked
                                                      : app->assets.tiles,
                          sprite, x, y, (float)TSW_TILE_SIZE);
}

/* The original's left-panel item images (Image4..18, a 3x5 grid at x 16 +
 * 32*col, y 192 + 32*row; slot s = row*3 + col shows sprite 0x23+s) and the
 * right-panel weapon/shield images (Image19/20 at (595,48) and (595,96),
 * sprites level+0x31 / level+0x36).  itemdisp repaints them whenever the
 * state changes, so draw them from the state every frame. */
static void draw_side_icons(App *app)
{
    if (!app->assets.tiles)
        return;
    /* Sub-dispatch 0xe hides slots 3..14 and the gear (event_engine.h). */
    bool hidden = app->events.engine.hud_hidden;
    for (int slot = 0; slot < SPECIAL_SLOT_COUNT; slot++) {
        if (hidden && slot >= 3)
            break;
        int value = state_get(&app->game.state, ST_SPECIAL_FIRST + slot);
        draw_panel_icon(app, value ? special_item_icon(slot) : -1,
                        16.0f + (slot % 3) * 32.0f,
                        192.0f + (slot / 3) * 32.0f);
    }
    if (hidden)
        return;
    int weapon = (int)game_stat(&app->game, ST_WEAPON_LEVEL);
    int shield = (int)game_stat(&app->game, ST_SHIELD_LEVEL);
    if (weapon > 0)
        draw_panel_icon(app, weapon + 0x31, 595.0f, 48.0f);
    if (shield > 0)
        draw_panel_icon(app, shield + 0x36, 595.0f, 96.0f);
}

/* The right panel's monster box (monshyouji, event_engine.h): Image31,
 * Memo8 and Memo5..7 at their DFM rectangles in the 640x400 layout. */
static void draw_monster_panel(App *app)
{
    const EventEngine *e = &app->events.engine;
    const EventMonsterPanel *p = &e->monster_panel;
    SDL_Renderer *r = app->render.renderer;
    TTF_Font *font = app->assets.font_hud ? app->assets.font_hud
                                          : app->assets.font;
    SDL_Color white = { 255, 255, 255, 255 };
    const char *name = e->have_messages
        ? messages_get(&e->messages, event_engine_monster_name_id(p->index))
        : NULL;
    if (name)
        draw_text_box_center(r, font, name, 524.0f, 268.0f, 103.0f, 20.0f,
                             white);
    const float ys[3] = { 288.0f, 308.0f, 328.0f };
    if (p->index < 0) {
        /* Captions ListBox2[0xd6..0xd8], left-justified. */
        for (int i = 0; i < 3; i++) {
            const char *t = e->have_messages
                ? messages_get(&e->messages, 0xd6 + i) : NULL;
            if (!t)
                continue;
            int w = 0, h = 0;
            if (font && TTF_GetStringSize(font, t, 0, &w, &h))
                draw_text(r, font, t, 559.0f, ys[i] + (20.0f - h) / 2.0f,
                          white);
        }
        return;
    }
    if (app->assets.tiles) {
        int pic = event_engine_monster_picture(p->index);
        if (pic >= 0)
            draw_panel_icon(app, pic, 561.0f, 230.0f);
    }
    const int32_t v[3] = { p->life, p->attack, p->defense };
    for (int i = 0; i < 3; i++) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%d", (int)v[i]);
        int w = 0, h = 0;
        if (font && TTF_GetStringSize(font, buf, 0, &w, &h))
            draw_text(r, font, buf, 557.0f + 70.0f - 3.0f - (float)w,
                      ys[i] + (20.0f - h) / 2.0f, white);
    }
}

/* The bottom console with the given lines (NULL: the log). */
static void draw_console_lines(App *app, const char *only)
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
    if (only) {
        if (*only)
            draw_text(r, app->assets.font, only, 8.0f, y, color);
        return;
    }
    /* The newest LOG_VISIBLE lines, or older ones when scrolled back
     * (PageUp/PageDown). */
    int last = app->log.count - app->log.scroll;
    int first = last - LOG_VISIBLE;
    if (first < 0) first = 0;
    for (int i = first; i < last; i++) {
        draw_text(r, app->assets.font, log_line(&app->log, i), 8.0f, y, color);
        y += 18.0f;
    }
}

static void draw_console(App *app)
{
    draw_console_lines(app, NULL);
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

static void draw_inventory(App *app)
{
    SDL_Renderer *r = app->render.renderer;
    TTF_Font *font = app->assets.font;
    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Color dim = { 150, 160, 180, 255 };
    char buf[96];

    draw_modal_panel(app, 360.0f, 360.0f, "Inventory");
    float grid_x = 170.0f;
    float grid_y = 110.0f;
    for (int slot = 0; slot < SPECIAL_SLOT_COUNT; slot++) {
        int col = slot % 3;
        int row = slot / 3;
        float x = grid_x + col * 48.0f;
        float y = grid_y + row * 48.0f;
        SDL_FRect cell = { x, y, 44.0f, 44.0f };
        if (slot == app->ui.inventory_selected) {
            SDL_SetRenderDrawColor(r, 92, 102, 132, 255);
            SDL_RenderFillRect(r, &cell);
        } else {
            SDL_SetRenderDrawColor(r, 52, 58, 76, 255);
            SDL_RenderRect(r, &cell);
        }
        int value = state_get(&app->game.state, ST_SPECIAL_FIRST + slot);
        if (value != 0) {
            draw_atlas_sprite(r, app->assets.tiles, special_item_icon(slot),
                              x + 6.0f, y + 6.0f, 32.0f);
            if (value > 1) {
                snprintf(buf, sizeof(buf), "x%d", value);
                draw_text(r, font, buf, x + 28.0f, y + 28.0f, white);
            }
        }
    }

    int selected = app->ui.inventory_selected;
    int value = state_get(&app->game.state, ST_SPECIAL_FIRST + selected);
    const char *name = value ? special_item_name(selected) : "Empty";
    draw_text_box_center(r, font, name, 150.0f, 360.0f, 340.0f, 24.0f, white);
    draw_text_box_center(r, font, "Enter use   Esc close",
                         150.0f, 390.0f, 340.0f, 20.0f, dim);
}

static void draw_forecast(App *app)
{
    draw_modal_panel(app, 520.0f, 400.0f, "Monster Forecast");
    SDL_Color line_color = { 225, 232, 245, 255 };
    float y = 96.0f;
    for (int i = 0; i < app->forecast_count && i < FORECAST_LINES; i++) {
        draw_text(app->render.renderer, app->assets.font,
                  app->forecast[i], 78.0f, y, line_color);
        y += 18.0f;
    }
    SDL_Color dim = { 150, 160, 180, 255 };
    draw_text_box_center(app->render.renderer, app->assets.font,
                         "Esc close", 60.0f, 404.0f, 520.0f, 20.0f, dim);
}

static void draw_game_over(App *app)
{
    draw_modal_panel(app, 380.0f, 160.0f, "Game Over");
    SDL_Color white = { 255, 255, 255, 255 };
    draw_text_box_center(app->render.renderer, app->assets.font,
                         "You died.", 130.0f, 220.0f, 380.0f, 24.0f, white);
    draw_text_box_center(app->render.renderer, app->assets.font,
                         "Enter new game   Esc quit",
                         130.0f, 252.0f, 380.0f, 20.0f, white);
}

static void draw_engine_dialog(App *app)
{
    draw_engine_cutscene(app);
    const EventEngine *re = &app->events.engine;
    if (re->ranking_open) {
        /* Form2: the area's ten rows (name, score, life, attack, defense);
         * the original layout is Phase 5. */
        SDL_Color text = { 225, 232, 245, 255 };
        SDL_Color hot = { 255, 230, 140, 255 };
        char buf[160];
        snprintf(buf, sizeof(buf), "Ranking  area %d", re->ranking_area);
        draw_modal_panel(app, 560.0f, 330.0f, buf);
        for (int i = 0; i < RANKING_ROWS; i++) {
            const int32_t *v = &re->ranking.v[i * 6];
            snprintf(buf, sizeof(buf), "%2d  %-18.18s %6d %8d %6d %6d", i + 1,
                     re->ranking.names[i], v[0], v[1], v[2], v[3]);
            bool mine = !re->ranking_name_entry && re->ranking_rank == i + 1;
            draw_text(app->render.renderer, app->assets.font, buf, 60.0f,
                      100.0f + 20.0f * (float)i, mine ? hot : text);
        }
        if (re->ranking_name_entry) {
            /* InputQuery: caption ui.tsv 6, prompt ui.tsv 7. */
            const char *prompt = re->have_ui_texts
                ? messages_get(&re->ui_texts, 7) : NULL;
            snprintf(buf, sizeof(buf), "%s: %s_",
                     prompt ? prompt : "Name", app->events.name_input);
            draw_text_box_center(app->render.renderer, app->assets.font, buf,
                                 60.0f, 320.0f, 520.0f, 20.0f, hot);
        } else {
            draw_text_box_center(app->render.renderer, app->assets.font,
                                 "Enter close", 60.0f, 320.0f, 520.0f, 20.0f,
                                 text);
        }
        if (!re->message_open)
            return;
    }
    if (re->message_open) {
        /* ShowMessage */
        SDL_Color text = { 225, 232, 245, 255 };
        draw_modal_panel(app, 480.0f, 120.0f, "Tower of the Sorcerer");
        draw_text_wrapped(app->render.renderer, app->assets.font,
                          re->message_text, 100.0f, 205.0f, 440.0f, text);
        draw_text_box_center(app->render.renderer, app->assets.font, "OK",
                             260.0f, 250.0f, 120.0f, 20.0f, text);
        return;
    }
    /* zyouout's "try again?" MessageDlg (lost event battle). */
    const EventEngine *dp = &app->events.engine;
    if (dp->death_prompt) {
        draw_modal_panel(app, 560.0f, 190.0f, "Tower of the Sorcerer");
        SDL_Color text = { 225, 232, 245, 255 };
        SDL_Color hot = { 255, 230, 140, 255 };
        draw_text_wrapped(app->render.renderer, app->assets.font,
                          dp->death_prompt_text[0] ? dp->death_prompt_text
                                                   : "Try again?",
                          80.0f, 185.0f, 480.0f, text);
        bool no = dp->choice_focus_no;
        draw_text_box_center(app->render.renderer, app->assets.font,
                             no ? "Yes" : "> Yes <", 160.0f, 300.0f, 140.0f,
                             20.0f, no ? text : hot);
        draw_text_box_center(app->render.renderer, app->assets.font,
                             no ? "> No <" : "No", 340.0f, 300.0f, 140.0f,
                             20.0f, no ? hot : text);
        return;
    }
    /* Item panel (form+0x2cc/0x2d0): Yes (Button38) / No (Button37). */
    if (dp->item_confirm) {
        static const int CASE_SLOT[15] = {
            -1, -1, -1, -1, -1, 4, 5, 6, 7, 8, 9, -1, 11, 12, 13
        };
        int c = dp->item_confirm_case;
        int slot = c >= 0 && c < 15 ? CASE_SLOT[c] : -1;
        char line[160];
        snprintf(line, sizeof(line), "Use the %s?", special_item_name(slot));
        draw_modal_panel(app, 420.0f, 130.0f, "Item");
        SDL_Color text = { 225, 232, 245, 255 };
        SDL_Color hot = { 255, 230, 140, 255 };
        draw_text_box_center(app->render.renderer, app->assets.font, line,
                             110.0f, 205.0f, 420.0f, 20.0f, text);
        bool no = dp->item_confirm_focus_no;
        draw_text_box_center(app->render.renderer, app->assets.font,
                             no ? "Yes" : "> Yes <", 170.0f, 245.0f, 120.0f,
                             20.0f, no ? text : hot);
        draw_text_box_center(app->render.renderer, app->assets.font,
                             no ? "> No <" : "No", 350.0f, 245.0f, 120.0f,
                             20.0f, no ? hot : text);
        return;
    }
    /* Orb of Flying: Button8 up / Button9 down / Button10 land. */
    if (dp->fly_open) {
        char line[160];
        snprintf(line, sizeof(line), "Floor %d   %s%s  Enter: land",
                 state_get(&app->game.state, ST_FLOOR),
                 dp->fly_up_enabled ? "Up: higher  " : "",
                 dp->fly_down_enabled ? "Down: lower" : "");
        draw_modal_panel(app, 420.0f, 90.0f, "Orb of Flying");
        SDL_Color text = { 225, 232, 245, 255 };
        draw_text_box_center(app->render.renderer, app->assets.font, line,
                             110.0f, 220.0f, 420.0f, 20.0f, text);
        return;
    }
    /* zyouout window (RichEdit1) driven by the event engine. */
    const char *msg = event_engine_dialog(&app->events.engine);
    if (!msg)
        return;
    draw_modal_panel(app, 560.0f, 190.0f, "Message");
    SDL_Color text = { 225, 232, 245, 255 };
    draw_text_wrapped(app->render.renderer, app->assets.font, msg,
                      80.0f, 185.0f, 480.0f, text);
    const EventEngine *en = &app->events.engine;
    if (en->altar_open) {
        /* Button39..42: the three offers and cancel. */
        SDL_Color hot = { 255, 230, 140, 255 };
        for (int i = 0; i < 4; i++) {
            char line[160];
            snprintf(line, sizeof(line), "%s%s", en->altar_focus == i ? "> " : "  ",
                     en->altar_options[i]);
            draw_text_box_center(app->render.renderer, app->assets.font, line,
                                 60.0f, 225.0f + 20.0f * (float)i, 520.0f, 20.0f,
                                 en->altar_focus == i ? hot : text);
        }
        return;
    }
    if (en->dialog_open && en->dialog_choice) {
        /* Button2 "Yes" / Button7 "No" (No disabled for the floor-2 gift). */
        SDL_Color dim = { 120, 126, 140, 255 };
        SDL_Color hot = { 255, 230, 140, 255 };
        bool no = en->choice_focus_no;
        draw_text_box_center(app->render.renderer, app->assets.font,
                             no ? "Yes" : "> Yes <", 160.0f, 300.0f, 140.0f,
                             20.0f, no ? text : hot);
        draw_text_box_center(app->render.renderer, app->assets.font,
                             no ? "> No <" : "No", 340.0f, 300.0f, 140.0f,
                             20.0f, !en->choice_no_enabled ? dim
                                                           : no ? hot : text);
    } else if (en->dialog_open) {
        draw_text_box_center(app->render.renderer, app->assets.font,
                             "Enter continue", 60.0f, 300.0f, 520.0f, 20.0f,
                             text);
    }
}

static void draw_event_dialog(App *app)
{
    draw_engine_dialog(app);
}

static void draw_menu(App *app)
{
    if (app->ui.view == UI_VIEW_INVENTORY) {
        draw_inventory(app);
        return;
    }
    if (app->ui.view == UI_VIEW_GAME_OVER) {
        draw_game_over(app);
        return;
    }
    if (app->ui.view == UI_VIEW_FORECAST) {
        draw_forecast(app);
        return;
    }

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
    if (renderer->dissolve_tex) SDL_DestroyTexture(renderer->dissolve_tex);
    if (renderer->capture) SDL_DestroySurface(renderer->capture);
    renderer->dissolve_tex = NULL;
    renderer->capture = NULL;
    if (renderer->renderer) SDL_DestroyRenderer(renderer->renderer);
    if (renderer->window) SDL_DestroyWindow(renderer->window);
    renderer->renderer = NULL;
    renderer->window = NULL;
}

static void draw_launch_intro(App *app)
{
    SDL_Renderer *r = app->render.renderer;
    SDL_FRect full = { 0.0f, 0.0f, (float)TSW_CANVAS_W,
                       (float)TSW_PLAYFIELD_H };
    if (app->assets.open0) {
        SDL_RenderTexture(r, app->assets.open0, NULL, &full);
    } else {
        SDL_Color white = { 230, 235, 245, 255 };
        draw_text_box_center(r, app->assets.font, "open0.bmp missing",
                             100.0f, 190.0f, (float)TSW_CANVAS_W, 24.0f,
                             white);
    }
    IntroFrame frame;
    intro_frame(app->intro.tick, &frame);
    SDL_Texture *hero = app->assets.intro_sprite[app->intro.tick & 1];
    if (hero && intro_draws_sprite(app->intro.tick)) {
        SDL_FRect dst = { frame.x, frame.y, frame.size, frame.size };
        SDL_RenderTexture(r, hero, NULL, &dst);
    }
}

void render_draw(App *app)
{
    if (!app || !app->render.renderer)
        return;

    SDL_Renderer *r = app->render.renderer;
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);

    if (app->intro.active) {
        draw_launch_intro(app);
        /* Only the status bar (Edit8) and its default text are shown. */
        draw_console_lines(app, app->intro_text);
        return;
    }

    SDL_FRect left = { 0.0f, 0.0f, 125.0f, 400.0f };
    SDL_FRect center = { 125.0f, 0.0f, 390.0f, 400.0f };
    SDL_FRect right = { 515.0f, 0.0f, 125.0f, 400.0f };
    if (app->assets.left) SDL_RenderTexture(r, app->assets.left, NULL, &left);
    if (app->assets.center) SDL_RenderTexture(r, app->assets.center, NULL, &center);
    if (app->assets.right) SDL_RenderTexture(r, app->assets.right, NULL, &right);

    draw_map(app);
    draw_engine_doors(app);
    draw_engine_moves(app);
    draw_engine_vampire(app);
    draw_player_sprite(app);
    /* op-0x09 composites go into the original's map buffer after the player
     * was drawn there, so one on the player's tile covers the player (the
     * opening's hit at (4,8)); any later player redraw drops it. */
    draw_event_actors(app);
    draw_engine_wipe(app);
    draw_center_picture(app);
    if (!app->events.engine.hud_hidden) {
        draw_hud(app);
        draw_monster_panel(app);
    }
    draw_side_icons(app);
    draw_console(app);
    if (app->ui.view != UI_VIEW_GAME)
        draw_menu(app);
    draw_event_dialog(app);
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
