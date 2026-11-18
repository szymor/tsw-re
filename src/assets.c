#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L     /* popen (fc-match) */
#endif
#include "assets.h"

#include <SDL3_image/SDL_image.h>

#include <stdio.h>
#include <stdlib.h>
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

/* --------------------------------------------------------------------- */
/* The system's Japanese font.  The game ships no CJK font; Japanese
 * systems have one, and it backs the sans face for the characters that
 * face lacks (TTF_AddFallbackFont). */

typedef struct {
    char path[ASSET_PATH_LEN];
    int face;
} FontFile;

/* `cmd` prints "<file>\n<index>" (fc-match); false when it does not. */
static bool fc_match(const char *cmd, FontFile *out)
{
#if defined(_WIN32) || defined(__APPLE__)
    (void)cmd;
    (void)out;
    return false;
#else
    FILE *p = popen(cmd, "r");
    if (!p)
        return false;
    char file[ASSET_PATH_LEN] = "", index[32] = "";
    bool ok = fgets(file, sizeof(file), p) && fgets(index, sizeof(index), p);
    pclose(p);
    file[strcspn(file, "\r\n")] = '\0';
    if (!ok || !file[0])
        return false;
    snprintf(out->path, sizeof(out->path), "%s", file);
    out->face = atoi(index);
    return true;
#endif
}

static TTF_Font *open_face(const char *path, int face, float size)
{
    SDL_PropertiesID props = SDL_CreateProperties();
    if (!props)
        return NULL;
    SDL_SetStringProperty(props, TTF_PROP_FONT_CREATE_FILENAME_STRING, path);
    SDL_SetFloatProperty(props, TTF_PROP_FONT_CREATE_SIZE_FLOAT, size);
    SDL_SetNumberProperty(props, TTF_PROP_FONT_CREATE_FACE_NUMBER, face);
    TTF_Font *font = TTF_OpenFontWithProperties(props);
    SDL_DestroyProperties(props);
    return font;
}

/* A collection (.ttc) holds the Chinese, Japanese and Korean cuts of one
 * design, which draw some shared characters differently: take its
 * Japanese face ("... JP") when it has one. */
static int japanese_face(const char *path, int face)
{
    for (int i = 0; i < 16; i++) {
        TTF_Font *f = open_face(path, i, 12.0f);
        if (!f)
            break;
        const char *family = TTF_GetFontFamilyName(f);
        bool jp = family && strstr(family, " JP") != NULL;
        TTF_CloseFont(f);
        if (jp)
            return i;
    }
    return face;
}

static bool find_japanese_font(bool bold, FontFile *out)
{
    static const char *const REGULAR[] = {
        "C:/Windows/Fonts/YuGothM.ttc", "C:/Windows/Fonts/meiryo.ttc",
        "C:/Windows/Fonts/msgothic.ttc",
        "/System/Library/Fonts/\xe3\x83\x92\xe3\x83\xa9\xe3\x82\xae\xe3\x83\x8e"
        "\xe8\xa7\x92\xe3\x82\xb4\xe3\x82\xb7\xe3\x83\x83\xe3\x82\xaf W3.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/ipafont-gothic/ipagp.ttf",
        NULL
    };
    static const char *const BOLD[] = {
        "C:/Windows/Fonts/YuGothB.ttc", "C:/Windows/Fonts/meiryob.ttc",
        "/System/Library/Fonts/\xe3\x83\x92\xe3\x83\xa9\xe3\x82\xae\xe3\x83\x8e"
        "\xe8\xa7\x92\xe3\x82\xb4\xe3\x82\xb7\xe3\x83\x83\xe3\x82\xaf W6.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Bold.ttc",
        NULL
    };
    /* fontconfig first (any distribution's layout), by family, then by
     * language; then the usual files of each system. */
    if (fc_match(bold ? "fc-match -f '%{file}\\n%{index}\\n' "
                        "'Noto Sans CJK JP:weight=bold' 2>/dev/null"
                      : "fc-match -f '%{file}\\n%{index}\\n' "
                        "'Noto Sans CJK JP' 2>/dev/null", out) &&
        strstr(out->path, "CJK")) {
        out->face = japanese_face(out->path, out->face);
        return true;
    }
    if (fc_match(bold ? "fc-match -f '%{file}\\n%{index}\\n' "
                        "':lang=ja:weight=bold' 2>/dev/null"
                      : "fc-match -f '%{file}\\n%{index}\\n' "
                        "':lang=ja' 2>/dev/null", out)) {
        TTF_Font *probe = open_face(out->path, out->face, 12.0f);
        bool kana = probe && TTF_FontHasGlyph(probe, 0x3042);   /* あ */
        if (probe)
            TTF_CloseFont(probe);
        if (kana) {
            out->face = japanese_face(out->path, out->face);
            return true;
        }
    }
    const char *const *list = bold ? BOLD : REGULAR;
    for (int i = 0; list[i]; i++) {
        TTF_Font *probe = open_face(list[i], 0, 12.0f);
        if (!probe)
            continue;
        TTF_CloseFont(probe);
        snprintf(out->path, sizeof(out->path), "%s", list[i]);
        out->face = japanese_face(list[i], 0);
        return true;
    }
    return false;
}

/* A Liberation face for the pictures' words: the bundled file first (the
 * look is the art's, not the system's), then the system's copy. */
static TTF_Font *open_liberation(const char *data_dir, const char *file,
                                 float size)
{
    char path[ASSET_PATH_LEN];
    const char *dirs[] = {
        NULL, "/usr/share/fonts/liberation",
        "/usr/share/fonts/truetype/liberation",
        "/usr/share/fonts/truetype/liberation2",
    };
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        if (dirs[i])
            snprintf(path, sizeof(path), "%s/%s", dirs[i], file);
        else
            snprintf(path, sizeof(path), "%s/fonts/%s", data_dir, file);
        TTF_Font *font = TTF_OpenFont(path, size);
        if (font)
            return font;
    }
    return NULL;
}

/* The Japanese fallback of `font` (size `size`), from `file`; NULL when
 * there is none. */
static TTF_Font *add_fallback(TTF_Font *font, const FontFile *file,
                              float size)
{
    if (!font || !file->path[0])
        return NULL;
    TTF_Font *fb = open_face(file->path, file->face, size);
    if (fb && !TTF_AddFallbackFont(font, fb)) {
        TTF_CloseFont(fb);
        fb = NULL;
    }
    return fb;
}

bool assets_latin_can_draw(const Assets *assets, const char *text)
{
    TTF_Font *f = assets ? assets->latin_probe : NULL;
    if (!f || !text)
        return false;
    const char *s = text;
    size_t left = strlen(text);
    while (left > 0) {
        Uint32 ch = SDL_StepUTF8(&s, &left);
        if (ch >= 0x20 && !TTF_FontHasGlyph(f, ch))
            return false;
    }
    return true;
}

bool assets_use_cjk(Assets *assets, bool on)
{
    if (!assets)
        return false;
    bool have = assets->cjk_font && assets->cjk_hud;
    for (int i = 0; i < UI_FONT_COUNT; i++)
        have = have && assets->cjk[i];
    bool cjk = on && have;
    for (int i = 0; i < UI_FONT_COUNT; i++)
        assets->ui[i] = cjk ? assets->cjk[i] : assets->latin_ui[i];
    assets->font = cjk ? assets->cjk_font : assets->latin_font;
    assets->font_hud = cjk ? assets->cjk_hud : assets->latin_hud;
    return cjk || !on;
}

bool assets_can_draw(const Assets *assets, const char *text)
{
    TTF_Font *main = assets ? (assets->latin_probe ? assets->latin_probe
                                                   : assets->ui[UI_FONT_14])
                            : NULL;
    if (!main || !text)
        return false;
    const char *s = text;
    size_t left = strlen(text);
    while (left > 0) {
        Uint32 ch = SDL_StepUTF8(&s, &left);
        if (ch == 0 || ch == SDL_INVALID_UNICODE_CODEPOINT)
            return false;
        if (ch < 0x20)
            continue;
        if (TTF_FontHasGlyph(main, ch))
            continue;
        if (!assets->cjk[UI_FONT_14] ||
            !TTF_FontHasGlyph(assets->cjk[UI_FONT_14], ch))
            return false;
    }
    return true;
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
    {
        /* open2 (open1 with the verse) is baked by pictures_bake. */
        static const char *const names[2] = { "open0", "open1" };
        for (int i = 0; i < 2; i++)
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
    load_texture(renderer, data_dir, "title05_clean", &assets->title05_clean,
                 NULL, 0);
    load_texture(renderer, data_dir, "title05R_clean",
                 &assets->title05r_clean, NULL, 0);
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

    /* The title's credits: Liberation Sans Bold whatever --font says, and
     * without the extra synthetic bold load_font gives its bold faces. */
    assets->credit_font = load_font(data_dir, NULL, 19.0f, true);
    if (assets->credit_font)
        TTF_SetFontStyle(assets->credit_font, TTF_STYLE_NORMAL);
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
    assets->latin_probe = load_font(data_dir, font_override, 14.0f, false);
    for (int i = 0; i < UI_FONT_COUNT; i++)
        assets->latin_ui[i] = assets->ui[i];
    assets->latin_font = assets->font;
    assets->latin_hud = assets->font_hud;
    /* The Japanese fallback (optional): its bold file where the system
     * has one, else the regular file emboldened like the sans face. */
    FontFile regular = { "", 0 }, bold = { "", 0 };
    if (find_japanese_font(false, &regular)) {
        snprintf(assets->cjk_path, sizeof(assets->cjk_path), "%s",
                 regular.path);
        if (!find_japanese_font(true, &bold))
            bold = regular;
        assets->cjk_font = add_fallback(assets->font, &regular, 14.0f);
        assets->cjk_hud = add_fallback(assets->font_hud, &bold, 14.0f);
        for (int i = 0; i < UI_FONT_COUNT; i++)
            assets->cjk[i] = add_fallback(assets->ui[i],
                                          UI[i].bold ? &bold : &regular,
                                          UI[i].size);
    }
    /* The intro's and ending's words (pictures.c; optional: without a
     * font the original pictures stay). */
    static const struct { const char *file; float size; bool bold; }
        PIC[PIC_FONT_COUNT] = {
        [PIC_FONT_INTRO]   = { "LiberationSerif-Regular.ttf", 30.0f, false },
        [PIC_FONT_ENDING]  = { "LiberationSerif-Regular.ttf", 33.0f, false },
        [PIC_FONT_THE_END] = { "LiberationSerif-BoldItalic.ttf", 72.0f, true },
        [PIC_FONT_CREDITS] = { "LiberationSans-Bold.ttf", 13.0f, true },
        [PIC_FONT_SEE_YOU] = { "LiberationSans-Bold.ttf", 18.0f, true },
    };
    for (int i = 0; i < PIC_FONT_COUNT; i++) {
        assets->pic_font[i] = open_liberation(data_dir, PIC[i].file,
                                              PIC[i].size);
        if (regular.path[0])
            assets->pic_font_cjk[i] = open_face(
                PIC[i].bold ? bold.path : regular.path,
                PIC[i].bold ? bold.face : regular.face, PIC[i].size);
    }
    /* Image21 without its verse is End26.bmp: plain white. */
    assets->image21_base = SDL_CreateSurface(640, 400, SDL_PIXELFORMAT_RGBA32);
    if (assets->image21_base)
        SDL_FillSurfaceRect(assets->image21_base, NULL,
                            SDL_MapSurfaceRGBA(assets->image21_base,
                                               255, 255, 255, 255));
    assets->image33_clean = load_surface(data_dir, "Image33_clean");
    assets->image34_clean = load_surface(data_dir, "Image34_clean");
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
    if (assets->title05_clean) SDL_DestroyTexture(assets->title05_clean);
    if (assets->title05r_clean) SDL_DestroyTexture(assets->title05r_clean);
    if (assets->credit_font) TTF_CloseFont(assets->credit_font);
    for (int i = 0; i < PIC_FONT_COUNT; i++) {
        if (assets->pic_font[i]) TTF_CloseFont(assets->pic_font[i]);
        if (assets->pic_font_cjk[i]) TTF_CloseFont(assets->pic_font_cjk[i]);
    }
    if (assets->image21_base) SDL_DestroySurface(assets->image21_base);
    if (assets->image33_clean) SDL_DestroySurface(assets->image33_clean);
    if (assets->image34_clean) SDL_DestroySurface(assets->image34_clean);
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
    /* ui / font / font_hud alias the sans or the CJK fonts (a failed
     * assets_init may have set only ui[] and font) */
    for (int i = 0; i < UI_FONT_COUNT; i++) {
        TTF_Font *f = assets->latin_ui[i] ? assets->latin_ui[i] : assets->ui[i];
        if (f && f != assets->cjk[i]) TTF_CloseFont(f);
    }
    TTF_Font *hud = assets->latin_hud ? assets->latin_hud : assets->font_hud;
    TTF_Font *body = assets->latin_font ? assets->latin_font : assets->font;
    if (hud && hud != assets->cjk_hud) TTF_CloseFont(hud);
    if (body && body != assets->cjk_font) TTF_CloseFont(body);
    if (assets->latin_probe) TTF_CloseFont(assets->latin_probe);
    /* the fallbacks after the fonts that use them */
    for (int i = 0; i < UI_FONT_COUNT; i++)
        if (assets->cjk[i]) TTF_CloseFont(assets->cjk[i]);
    if (assets->cjk_hud) TTF_CloseFont(assets->cjk_hud);
    if (assets->cjk_font) TTF_CloseFont(assets->cjk_font);
    memset(assets, 0, sizeof(*assets));
}
