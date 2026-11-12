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
    /* Optional opening/cutscene frames.  Their absence falls back to text. */
    load_texture(renderer, data_dir, "open0", &assets->open0, err, errsz);
    load_texture(renderer, data_dir, "open1", &assets->open1, err, errsz);
    load_texture(renderer, data_dir, "open2", &assets->open2, err, errsz);

    assets->font = load_font(data_dir, font_override, 14.0f, false);
    assets->font_hud = load_font(data_dir, font_override, 14.0f, true);
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
    if (assets->keys) SDL_DestroyTexture(assets->keys);
    if (assets->player) SDL_DestroyTexture(assets->player);
    if (assets->open0) SDL_DestroyTexture(assets->open0);
    if (assets->open1) SDL_DestroyTexture(assets->open1);
    if (assets->open2) SDL_DestroyTexture(assets->open2);
    if (assets->font_hud) TTF_CloseFont(assets->font_hud);
    if (assets->font) TTF_CloseFont(assets->font);
    memset(assets, 0, sizeof(*assets));
}
