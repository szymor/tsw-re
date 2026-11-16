#include "assets.h"

#include <SDL3_image/SDL_image.h>

#include <stdio.h>
#include <string.h>

static bool load_texture(SDL_Renderer *renderer, const char *data_dir,
                         const char *name, SDL_Texture **out,
                         char *err, size_t errsz)
{
    char path[ASSET_PATH_LEN];
    const char *exts[] = { "png", "bmp" };
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        snprintf(path, sizeof(path), "%s/gfx/%s.%s", data_dir, name, exts[i]);
        SDL_Texture *tex = IMG_LoadTexture(renderer, path);
        if (tex) {
            SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
            *out = tex;
            return true;
        }
    }
    if (err && errsz)
        snprintf(err, errsz, "cannot load %s/gfx/%s.png (%s)",
                 data_dir, name, SDL_GetError());
    return false;
}

/* A gfx image as an RGBA32 surface, or NULL. */
static SDL_Surface *load_surface(const char *data_dir, const char *name)
{
    char path[ASSET_PATH_LEN + 32];
    const char *exts[] = { "png", "bmp" };
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        snprintf(path, sizeof(path), "%s/gfx/%s.%s", data_dir, name, exts[i]);
        SDL_Surface *raw = IMG_Load(path);
        if (raw) {
            SDL_Surface *s = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(raw);
            return s;
        }
    }
    return NULL;
}

/* Compose one launch-intro frame from the Chtable atlas: the mask tile's
 * black pixels select the sprite tile, white stays transparent (the same
 * stencil kasanegaki applies through CopyMode). */
static void make_intro_sprite(Assets *assets, SDL_Renderer *renderer,
                              int index, int sprite_tile, int mask_tile)
{
    SDL_Surface *atlas = assets->tiles_surface;
    if (!atlas || !atlas->pixels)
        return;
    const int tile = 32;
    int cols = atlas->w / tile;
    int rows = atlas->h / tile;
    if (cols <= 0 || rows <= 0)
        return;
    int si = sprite_tile - 1;
    int mi = mask_tile - 1;
    if (si < 0 || mi < 0 || si >= cols * rows || mi >= cols * rows)
        return;

    SDL_Surface *out = SDL_CreateSurface(tile, tile, SDL_PIXELFORMAT_RGBA32);
    if (!out)
        return;
    if (!SDL_LockSurface(atlas)) {
        SDL_DestroySurface(out);
        return;
    }
    if (!SDL_LockSurface(out)) {
        SDL_UnlockSurface(atlas);
        SDL_DestroySurface(out);
        return;
    }
    const Uint8 *sp = (const Uint8 *)atlas->pixels;
    const Uint8 *mp = (const Uint8 *)atlas->pixels;
    Uint8 *dp = (Uint8 *)out->pixels;
    int sx = (si % cols) * tile;
    int sy = (si / cols) * tile;
    int mx = (mi % cols) * tile;
    int my = (mi / cols) * tile;
    for (int y = 0; y < tile; y++) {
        for (int x = 0; x < tile; x++) {
            const Uint8 *s = sp + (sy + y) * atlas->pitch + (sx + x) * 4;
            const Uint8 *m = mp + (my + y) * atlas->pitch + (mx + x) * 4;
            int lum = (m[0] + m[1] + m[2]) / 3;
            Uint8 *d = dp + y * out->pitch + x * 4;
            if (lum < 128) {
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                d[3] = 255;
            } else {
                d[0] = d[1] = d[2] = d[3] = 0;
            }
        }
    }
    SDL_UnlockSurface(out);
    SDL_UnlockSurface(atlas);
    assets->intro_sprite[index] = SDL_CreateTextureFromSurface(renderer, out);
    if (assets->intro_sprite[index])
        SDL_SetTextureScaleMode(assets->intro_sprite[index],
                                SDL_SCALEMODE_NEAREST);
    SDL_DestroySurface(out);
}

static TTF_Font *load_font(const char *data_dir, const char *override,
                           float size, bool bold)
{
    char path[ASSET_PATH_LEN];
    if (override && *override) {
        TTF_Font *font = TTF_OpenFont(override, size);
        if (font) {
            if (bold) TTF_SetFontStyle(font, TTF_STYLE_BOLD);
            return font;
        }
    }
    const char *regular[] = {
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/msttcorefonts/Verdana.ttf",
        "/usr/share/fonts/truetype/msttcorefonts/verdana.ttf",
        "/usr/share/fonts/TTF/Verdana.ttf",
        NULL
    };
    const char *bold_faces[] = {
        "/usr/share/fonts/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf",
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/msttcorefonts/Verdana_Bold.ttf",
        "/usr/share/fonts/truetype/msttcorefonts/verdanab.ttf",
        "/usr/share/fonts/TTF/Verdana_Bold.ttf",
        NULL
    };
    const char **candidates = bold ? bold_faces : regular;
    for (int i = 0; candidates[i]; i++) {
        TTF_Font *font = TTF_OpenFont(candidates[i], size);
        if (font) {
            if (bold) TTF_SetFontStyle(font, TTF_STYLE_BOLD);
            return font;
        }
    }
    snprintf(path, sizeof(path), "%s/fonts/%s", data_dir,
             bold ? "LiberationSans-Bold.ttf" : "LiberationSans-Regular.ttf");
    TTF_Font *font = TTF_OpenFont(path, size);
    if (font && bold) TTF_SetFontStyle(font, TTF_STYLE_BOLD);
    return font;
}

bool assets_init(Assets *assets, SDL_Renderer *renderer,
                 const char *data_dir, const char *font_override,
                 char *err, size_t errsz)
{
    memset(assets, 0, sizeof(*assets));
    snprintf(assets->data_dir, sizeof(assets->data_dir), "%s", data_dir);

    if (!load_texture(renderer, data_dir, "Left", &assets->left, err, errsz) ||
        !load_texture(renderer, data_dir, "CENTER", &assets->center, err, errsz) ||
        !load_texture(renderer, data_dir, "Right", &assets->right, err, errsz) ||
        !load_texture(renderer, data_dir, "Chtable", &assets->tiles, err, errsz) ||
        !load_texture(renderer, data_dir, "Key", &assets->keys, err, errsz) ||
        !load_texture(renderer, data_dir, "Player", &assets->player, err, errsz)) {
        assets_shutdown(assets);
        return false;
    }

    /* CPU copy of the atlas for kasanegaki-style composed tiles. */
    {
        char path[ASSET_PATH_LEN + 32];
        snprintf(path, sizeof(path), "%s/gfx/Chtable.png", data_dir);
        SDL_Surface *raw = IMG_Load(path);
        if (raw) {
            assets->tiles_surface =
                SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(raw);
        }
    }
    /* Chtableb is the sprite bitmask atlas (black = sprite, white =
     * background; 01-binary-and-assets.md).  Fold it into an alpha channel
     * so a sprite can be drawn over any background, as ImageList1 draws
     * it. */
    if (assets->tiles_surface) {
        char path[ASSET_PATH_LEN + 32];
        snprintf(path, sizeof(path), "%s/gfx/Chtableb.png", data_dir);
        SDL_Surface *raw = IMG_Load(path);
        SDL_Surface *mask = raw ? SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32) : NULL;
        if (raw) SDL_DestroySurface(raw);
        SDL_Surface *out = mask ? SDL_DuplicateSurface(assets->tiles_surface) : NULL;
        if (out) {
            for (int y = 0; y < out->h; y++) {
                Uint8 *d = (Uint8 *)out->pixels + y * out->pitch;
                const Uint8 *m = y < mask->h
                    ? (const Uint8 *)mask->pixels + y * mask->pitch : NULL;
                /* ImageList1 takes the mask as a monochrome bitmap:
                 * only pure white is background; the few grey pixels in
                 * Chtableb (tiles 62, 63, 66, 115, 129, 141) are sprite.
                 * Rows past the mask (tiles 165..) have none. */
                for (int x = 0; x < out->w; x++) {
                    const Uint8 *mp = m && x < mask->w ? m + x * 4 : NULL;
                    bool white = mp && mp[0] == 255 && mp[1] == 255 &&
                                 mp[2] == 255;
                    d[x * 4 + 3] = white ? 0 : 255;
                }
            }
            assets->tiles_masked = SDL_CreateTextureFromSurface(renderer, out);
            if (assets->tiles_masked)
                SDL_SetTextureScaleMode(assets->tiles_masked, SDL_SCALEMODE_NEAREST);
            SDL_DestroySurface(out);
        }
        if (mask) SDL_DestroySurface(mask);
    }
    /* Optional opening/cutscene frames.  Their absence falls back to text. */
    load_texture(renderer, data_dir, "open0", &assets->open0, err, errsz);
    load_texture(renderer, data_dir, "open1", &assets->open1, err, errsz);
    load_texture(renderer, data_dir, "open2", &assets->open2, err, errsz);
    {
        static const char *const names[3] = { "open0", "open1", "open2" };
        for (int i = 0; i < 3; i++)
            assets->open_surface[i] = load_surface(data_dir, names[i]);
        /* syokidata2 (0x4552f6..0x455532): Left at x 0, CENTER at 125,
         * Right at 515 into the 640x400 bitmap 0x48c528. */
        for (int variant = 0; variant < 2; variant++) {
            SDL_Surface *parts[3] = {
                load_surface(data_dir, variant ? "LeftR" : "Left"),
                load_surface(data_dir, "CENTER"),
                load_surface(data_dir, variant ? "RightR" : "Right") };
            if (parts[0] && parts[1] && parts[2]) {
                SDL_Surface *p = SDL_CreateSurface(640, 400,
                                                   SDL_PIXELFORMAT_RGBA32);
                if (p) {
                    SDL_FillSurfaceRect(p, NULL,
                                        SDL_MapSurfaceRGBA(p, 0, 0, 0, 255));
                    int x = 0;
                    for (int i = 0; i < 3; i++) {
                        SDL_Rect dst = { x, 0, parts[i]->w, parts[i]->h };
                        SDL_BlitSurface(parts[i], NULL, p, &dst);
                        x += parts[i]->w;
                    }
                    if (variant)
                        assets->panels_surface_r = p;
                    else
                        assets->panels_surface = p;
                }
            }
            for (int i = 0; i < 3; i++)
                if (parts[i]) SDL_DestroySurface(parts[i]);
        }
    }
    /* The ending's pictures (tools/convert_assets.py); optional. */
    for (int i = 0; i < 16; i++) {
        char name[16];
        snprintf(name, sizeof(name), "End%02d", i + 5);
        load_texture(renderer, data_dir, name, &assets->end[i], NULL, 0);
    }
    load_texture(renderer, data_dir, "End26", &assets->end26, NULL, 0);
    load_texture(renderer, data_dir, "title05", &assets->title05, NULL, 0);
    load_texture(renderer, data_dir, "title05R", &assets->title05r, NULL, 0);
    load_texture(renderer, data_dir, "Image21", &assets->image21, NULL, 0);
    load_texture(renderer, data_dir, "Image33", &assets->image33, NULL, 0);
    load_texture(renderer, data_dir, "Image34", &assets->image34, NULL, 0);
    load_texture(renderer, data_dir, "LeftR", &assets->left_r, NULL, 0);
    load_texture(renderer, data_dir, "RightR", &assets->right_r, NULL, 0);
    for (int i = 0; i < 5; i++) {
        char name[32];
        snprintf(name, sizeof(name), "RankGlyph%d", i + 1);
        load_texture(renderer, data_dir, name, &assets->rank_glyph[i], NULL, 0);
    }

    /* Launch intro hero frames from Chtable (ImageList1 is built from the
     * same atlas by syokidata0). */
    if (assets->tiles_surface) {
        make_intro_sprite(assets, renderer, 0, 217, 210);
        make_intro_sprite(assets, renderer, 1, 218, 212);
    }

    assets->font = load_font(data_dir, font_override, 14.0f, false);
    assets->font_hud = load_font(data_dir, font_override, 14.0f, true);
    static const struct { float size; bool bold; } UI[UI_FONT_COUNT] = {
        { 12.0f, false }, { 12.0f, true }, { 13.0f, false }, { 14.0f, false },
        { 14.0f, true }, { 16.0f, false }, { 16.0f, true },
    };
    for (int i = 0; i < UI_FONT_COUNT; i++) {
        assets->ui[i] = load_font(data_dir, font_override, UI[i].size,
                                  UI[i].bold);
        if (!assets->ui[i]) {
            if (err && errsz)
                snprintf(err, errsz, "cannot open a font (%s)",
                         SDL_GetError());
            assets_shutdown(assets);
            return false;
        }
    }
    if (!assets->font || !assets->font_hud) {
        if (err && errsz)
            snprintf(err, errsz, "cannot open a font (%s)", SDL_GetError());
        assets_shutdown(assets);
        return false;
    }
    return true;
}

void assets_shutdown(Assets *assets)
{
    if (!assets) return;
    if (assets->left) SDL_DestroyTexture(assets->left);
    if (assets->center) SDL_DestroyTexture(assets->center);
    if (assets->right) SDL_DestroyTexture(assets->right);
    if (assets->tiles) SDL_DestroyTexture(assets->tiles);
    if (assets->tiles_surface) SDL_DestroySurface(assets->tiles_surface);
    if (assets->tiles_masked) SDL_DestroyTexture(assets->tiles_masked);
    if (assets->keys) SDL_DestroyTexture(assets->keys);
    if (assets->player) SDL_DestroyTexture(assets->player);
    if (assets->open0) SDL_DestroyTexture(assets->open0);
    if (assets->open1) SDL_DestroyTexture(assets->open1);
    if (assets->open2) SDL_DestroyTexture(assets->open2);
    for (int i = 0; i < 16; i++)
        if (assets->end[i]) SDL_DestroyTexture(assets->end[i]);
    if (assets->end26) SDL_DestroyTexture(assets->end26);
    if (assets->title05) SDL_DestroyTexture(assets->title05);
    if (assets->title05r) SDL_DestroyTexture(assets->title05r);
    if (assets->image21) SDL_DestroyTexture(assets->image21);
    if (assets->image33) SDL_DestroyTexture(assets->image33);
    if (assets->image34) SDL_DestroyTexture(assets->image34);
    for (int i = 0; i < 5; i++)
        if (assets->rank_glyph[i]) SDL_DestroyTexture(assets->rank_glyph[i]);
    if (assets->panels_surface) SDL_DestroySurface(assets->panels_surface);
    if (assets->panels_surface_r) SDL_DestroySurface(assets->panels_surface_r);
    if (assets->left_r) SDL_DestroyTexture(assets->left_r);
    if (assets->right_r) SDL_DestroyTexture(assets->right_r);
    for (int i = 0; i < 3; i++)
        if (assets->open_surface[i]) SDL_DestroySurface(assets->open_surface[i]);
    for (int i = 0; i < 2; i++)
        if (assets->intro_sprite[i]) SDL_DestroyTexture(assets->intro_sprite[i]);
    for (int i = 0; i < UI_FONT_COUNT; i++)
        if (assets->ui[i]) TTF_CloseFont(assets->ui[i]);
    if (assets->font_hud) TTF_CloseFont(assets->font_hud);
    if (assets->font) TTF_CloseFont(assets->font);
    memset(assets, 0, sizeof(*assets));
}
