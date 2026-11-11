#ifndef TSW_APP_H
#define TSW_APP_H

#include "assets.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "render.h"
#include "state.h"
#include "ui.h"

#include <SDL3/SDL.h>

#include <stdbool.h>
#include <stddef.h>

#define APP_PATH_LEN 1024
#define APP_PATH_BUF (APP_PATH_LEN + 32)
#define FORECAST_LINES 16
#define FORECAST_LINE_LEN 160

struct lua_State;

typedef struct App {
    Renderer render;
    Assets assets;
    Game game;
    MapBlock initial_map;
    bool have_initial_map;
    LogBuffer log;
    UiState ui;
    struct lua_State *lua;

    bool running;
    char forecast[FORECAST_LINES][FORECAST_LINE_LEN];
    int forecast_count;

    bool step_active;
    float step_from_x;
    float step_from_y;
    Uint64 step_start_ms;
    Uint64 (*clock_fn)(void);

    char data_dir[APP_PATH_LEN];
    char save_dir[APP_PATH_LEN];
    char save_path[APP_PATH_LEN];
    char font_path[APP_PATH_LEN];
} App;

bool app_init(App *app, const char *data_dir, const char *save_path,
              const char *save_dir, const char *font_path,
              char *err, size_t errsz);
void app_run(App *app);
void app_shutdown(App *app);

Uint64 app_now(const App *app);
bool app_new_game(App *app, char *err, size_t errsz);
bool app_save_slot_path(const App *app, int slot, char *out, size_t outsz);
bool app_save_slot_exists(const App *app, int slot);
bool app_save_slot(App *app, int slot, char *err, size_t errsz);
bool app_load_slot(App *app, int slot, char *err, size_t errsz);

#endif
