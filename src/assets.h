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
    UI_FONT_14,
    UI_FONT_14B,
    UI_FONT_16B,
    UI_FONT_COUNT
} UiFont;

typedef struct {
    SDL_Texture *left;
    SDL_Texture *center;
    SDL_Texture *right;
    SDL_Texture *tiles;
    SDL_Texture *keys;
    SDL_Texture *player;
    SDL_Texture *open0;
    SDL_Texture *open1;
    SDL_Texture *open2;
    /* Sub-dispatch 7's title pictures (optional). */
    SDL_Texture *title05;
    SDL_Texture *title05r;
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
     * open0..open2 (NULL when missing). */
    SDL_Surface *panels_surface;
    SDL_Surface *open_surface[3];
    /* The floor-50 ending (optional): End05..End20 (end[0..15]), End26,
     * and the form's pictures Image21 (closing text), Image33 ("The
     * End"), Image34 (the "back side" button). */
    SDL_Texture *end[16];
    SDL_Texture *end26;
    SDL_Texture *image21;
    SDL_Texture *image33;
    SDL_Texture *image34;
    /* Form2's BitBtn1..5 glyphs (RankGlyph1..5.png, optional). */
    SDL_Texture *rank_glyph[5];
    TTF_Font *font;
    TTF_Font *font_hud;
    TTF_Font *ui[UI_FONT_COUNT];     /* never NULL after assets_init */
    char data_dir[ASSET_PATH_LEN];
} Assets;

bool assets_init(Assets *assets, SDL_Renderer *renderer,
                 const char *data_dir, const char *font_override,
                 char *err, size_t errsz);
void assets_shutdown(Assets *assets);

#endif
