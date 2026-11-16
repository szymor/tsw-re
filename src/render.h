#ifndef TSW_RENDER_H
#define TSW_RENDER_H

#include <SDL3/SDL.h>

#include <stdbool.h>
#include <stddef.h>

#define TSW_CANVAS_W 640
#define TSW_CANVAS_H 480
#define TSW_PLAYFIELD_H 400
#define TSW_CONSOLE_H 80
#define TSW_TILE_SIZE 32
#define TSW_MAP_X 144
#define TSW_MAP_Y 24

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    /* openning9 dissolve: the last composed frame (key = image, mask and
     * backdrop) and the screen sub-dispatch 0xf captured (capture_seq =
     * the engine's screen_captures it belongs to). */
    SDL_Texture *dissolve_tex;
    int dissolve_key;
    SDL_Surface *capture;
    int capture_seq;
} Renderer;

struct App;

bool render_init(Renderer *renderer, const char *title,
                 char *err, size_t errsz);
void render_shutdown(Renderer *renderer);
void render_draw(struct App *app);
void render_present(Renderer *renderer);
void render_frame(struct App *app);
/* One 32x32 panel picture as itemdisp paints it (render.c). */
void render_panel_icon(struct App *app, int sprite, float x, float y);
bool render_save_png(Renderer *renderer, const char *path,
                     char *err, size_t errsz);

#endif
