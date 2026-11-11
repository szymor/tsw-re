#ifndef TSW_ASSETS_H
#define TSW_ASSETS_H

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <stdbool.h>
#include <stddef.h>

#define ASSET_PATH_LEN 1024

typedef struct {
    SDL_Texture *left;
    SDL_Texture *center;
    SDL_Texture *right;
    SDL_Texture *tiles;
    SDL_Texture *keys;
    SDL_Texture *player;
    TTF_Font *font;
    TTF_Font *font_hud;
    char data_dir[ASSET_PATH_LEN];
} Assets;

bool assets_init(Assets *assets, SDL_Renderer *renderer,
                 const char *data_dir, const char *font_override,
                 char *err, size_t errsz);
void assets_shutdown(Assets *assets);

#endif
