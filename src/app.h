#ifndef TSW_APP_H
#define TSW_APP_H

#include "assets.h"
#include "log.h"
#include "map.h"
#include "render.h"
#include "state.h"
#include "ui.h"

#include <SDL3/SDL.h>

#include <stdbool.h>
#include <stddef.h>

#define APP_PATH_LEN 1024

struct lua_State;

typedef struct App {
    Renderer render;
    Assets assets;
    MapBlock map;
    StateBlock state;
    LogBuffer log;
    UiState ui;
    struct lua_State *lua;

    int floor;
    int player_x;
    int player_y;
    bool have_state;
    bool running;

    char data_dir[APP_PATH_LEN];
    char save_path[APP_PATH_LEN];
    char font_path[APP_PATH_LEN];
} App;

bool app_init(App *app, const char *data_dir, const char *save_path,
              const char *font_path, char *err, size_t errsz);
void app_run(App *app);
void app_shutdown(App *app);

#endif
