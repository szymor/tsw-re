#ifndef TSW_ASSETS_H
#define TSW_ASSETS_H

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <stdbool.h>
#include <stddef.h>

#define ASSET_PATH_LEN 1024

/* The windows' fonts: the remake's sans face (load_font) in a few sizes. */
typedef enum {
    UI_FONT_12,
    UI_FONT_12B,
    UI_FONT_13,
    UI_FONT_14,
    UI_FONT_14B,
    UI_FONT_16,
    UI_FONT_16B,
    UI_FONT_COUNT
} UiFont;

/* The fonts of the words on the intro's and ending's pictures. */
enum {
    PIC_FONT_INTRO,      /* open2's verse: Liberation Serif 30 */
    PIC_FONT_ENDING,     /* Image21's verse: Liberation Serif 33 */
    PIC_FONT_THE_END,    /* Image33's "The End": Serif Bold Italic 72 */
    PIC_FONT_CREDITS,    /* Image33's credits: Liberation Sans Bold 13 */
    PIC_FONT_SEE_YOU,    /* "See you again!": Liberation Sans Bold 18 */
    PIC_FONT_COUNT
};

typedef struct {
    SDL_Texture *left;
    SDL_Texture *center;
    SDL_Texture *right;
    /* LeftR.bmp / RightR.bmp: the side panels of a second run (state
     * ST_OPTION_A set; syokidata2, openning31); NULL when missing. */
    SDL_Texture *left_r;
    SDL_Texture *right_r;
    SDL_Texture *tiles;
    SDL_Texture *keys;
    SDL_Texture *player;
    SDL_Texture *open0;
    SDL_Texture *open1;
    SDL_Texture *open2;             /* baked: open1 + the verse (pictures.c) */
    /* Sub-dispatch 7's title pictures, title05.bmp and title05R.bmp,
     * without their credit lettering (tools/make_clean_titles.py): the
     * game writes the credits over them in credit_font (render.c). */
    SDL_Texture *title05_clean;
    SDL_Texture *title05r_clean;
    TTF_Font *credit_font;
    /* The words written over the intro's and ending's pictures
     * (pictures.c), in the bundled Liberation Serif and Sans Bold, and
     * the system's Japanese face at the same sizes (NULL when none). */
    TTF_Font *pic_font[PIC_FONT_COUNT];
    TTF_Font *pic_font_cjk[PIC_FONT_COUNT];
    /* The art they are written over: plain white (Image21 without its
     * verse, the original's End26.bmp)
     * and Image33 / Image34 without their lettering
     * (tools/make_clean_pictures.py); open1 is open_surface[1]. */
    SDL_Surface *image21_base;
    SDL_Surface *image33_clean;
    SDL_Surface *image34_clean;
    /* Launch-intro hero frames (timer4ontimer): Chtable sprite 217/218
     * through mask 210/212, pre-composited to RGBA. */
    SDL_Texture *intro_sprite[2];
    /* CPU copy of Chtable (RGBA32) for composed tiles (op 0x09). */
    SDL_Surface *tiles_surface;
    /* Chtable with alpha = 255 - Chtableb (the sprite bitmask, black =
     * sprite): sprites without their floor, for moves and masked doors.
     * NULL if Chtableb is missing. */
    SDL_Texture *tiles_masked;
    /* CPU copies (RGBA32, 640x400) for openning9's dissolve: the bare
     * panels as syokidata2 composes 0x48c528 (Left | CENTER | Right) and
     * open0, open1 and the baked open2 (NULL when missing). */
    SDL_Surface *panels_surface;
    SDL_Surface *panels_surface_r;   /* LeftR | CENTER | RightR */
    SDL_Surface *open_surface[3];
    /* The floor-50 ending (optional): End05..End20 (end[0..15]) and the
     * form's pictures Image21 (closing text), Image33 ("The
     * End"), Image34 (the "back side" button), baked by pictures.c from
     * white and the cleaned Image33 / Image34. */
    SDL_Texture *end[16];
    SDL_Texture *image21;
    SDL_Texture *image33;
    SDL_Texture *image34;
    /* Form2's BitBtn1..5 glyphs (RankGlyph1..5.png, optional). */
    SDL_Texture *rank_glyph[5];
    TTF_Font *font;
    TTF_Font *font_hud;
    TTF_Font *ui[UI_FONT_COUNT];     /* never NULL after assets_init */
    /* The system's Japanese font, a fallback of each font above for the
     * characters the sans face lacks (assets_init; NULL entries when the
     * system has none).  cjk[i] backs ui[i]; cjk_font / cjk_hud back font
     * / font_hud. */
    TTF_Font *cjk[UI_FONT_COUNT];
    TTF_Font *cjk_font;
    TTF_Font *cjk_hud;
    /* The sans fonts themselves: ui / font / font_hud point at these, or
     * at the cjk ones while a CJK language is shown (assets_use_cjk). */
    TTF_Font *latin_ui[UI_FONT_COUNT];
    TTF_Font *latin_font;
    TTF_Font *latin_hud;
    /* The sans face without a fallback (TTF_FontHasGlyph asks the
     * fallbacks too): what it can draw by itself. */
    TTF_Font *latin_probe;
    char cjk_path[ASSET_PATH_LEN];   /* "" when none was found */
    char data_dir[ASSET_PATH_LEN];
} Assets;

bool assets_init(Assets *assets, SDL_Renderer *renderer,
                 const char *data_dir, const char *font_override,
                 char *err, size_t errsz);
void assets_shutdown(Assets *assets);
/* True when the window fonts (with their fallback) can draw every
 * character of the UTF-8 `text`. */
bool assets_can_draw(const Assets *assets, const char *text);
/* True when the sans face alone draws all of `text`. */
bool assets_latin_can_draw(const Assets *assets, const char *text);
/* Draw everything with the Japanese font (its own Latin letters and
 * digits sit on the same baseline as its kana), or with the sans face
 * again; false when there is no Japanese font. */
bool assets_use_cjk(Assets *assets, bool on);

#endif
