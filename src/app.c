#include "app.h"

#include "events.h"
#include "input.h"
#include "save.h"

#include <SDL3_image/SDL_image.h>

#include <stdio.h>
#include <string.h>

static void event_log(void *user, const char *message)
{
    App *app = user;
    if (app && message)
        log_add(&app->log, "%s", message);
}

static bool load_map_file(App *app, const char *path, char *err, size_t errsz)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (err && errsz) snprintf(err, errsz, "cannot open %s", path);
        return false;
    }
    uint8_t data[MAP_BLOCK_SIZE + 1];
    size_t n = fread(data, 1, sizeof(data), f);
    fclose(f);
    return map_parse(data, n, &app->map, err, errsz);
}

static bool find_smoke_script(const char *data_dir, char *out, size_t outsz)
{
    char path[APP_PATH_LEN];
    snprintf(path, sizeof(path), "%s/events/smoke.lua", data_dir);
    if (events_file_exists(path)) {
        snprintf(out, outsz, "%s", path);
        return true;
    }
    snprintf(path, sizeof(path), "src/events/smoke.lua");
    if (events_file_exists(path)) {
        snprintf(out, outsz, "%s", path);
        return true;
    }
    snprintf(path, sizeof(path), "events/smoke.lua");
    if (events_file_exists(path)) {
        snprintf(out, outsz, "%s", path);
        return true;
    }
    return false;
}

static void cleanup_partial(App *app)
{
    if (app->lua)
        events_shutdown(app->lua);
    app->lua = NULL;
    assets_shutdown(&app->assets);
    render_shutdown(&app->render);
    TTF_Quit();
    SDL_Quit();
}

bool app_init(App *app, const char *data_dir, const char *save_path,
              const char *font_path, char *err, size_t errsz)
{
    if (!app || !data_dir || !*data_dir) {
        if (err && errsz) snprintf(err, errsz, "missing data directory");
        return false;
    }

    memset(app, 0, sizeof(*app));
    snprintf(app->data_dir, sizeof(app->data_dir), "%s", data_dir);
    snprintf(app->save_path, sizeof(app->save_path), "%s", save_path ? save_path : "");
    snprintf(app->font_path, sizeof(app->font_path), "%s", font_path ? font_path : "");
    log_init(&app->log);
    ui_init(&app->ui);
    app->floor = 1;
    app->player_x = 5;
    app->player_y = 10;
    app->running = true;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        if (err && errsz)
            snprintf(err, errsz, "SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    if (!TTF_Init()) {
        if (err && errsz)
            snprintf(err, errsz, "TTF_Init failed: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }

    if (!render_init(&app->render, "Tower of the Sorcerer - reimplementation",
                     err, errsz)) {
        cleanup_partial(app);
        return false;
    }
    if (!assets_init(&app->assets, app->render.renderer, data_dir,
                     font_path, err, errsz)) {
        cleanup_partial(app);
        return false;
    }

    bool loaded_save = false;
    if (*app->save_path) {
        SaveData save;
        char save_err[256] = "";
        if (save_load_file(app->save_path, &save, save_err, sizeof(save_err))) {
            app->map = save.map;
            app->state = save.state;
            app->have_state = true;
            app->floor = state_get(&app->state, ST_FLOOR);
            if (app->floor < 1 || app->floor > 50)
                app->floor = 1;
            app->player_x = state_get(&app->state, ST_X);
            app->player_y = state_get(&app->state, ST_Y);
            if (app->player_x < 0 || app->player_x >= MAP_TILE_COLS)
                app->player_x = 5;
            if (app->player_y < 0 || app->player_y >= MAP_TILE_ROWS)
                app->player_y = 10;
            loaded_save = true;
            log_add(&app->log, "Save: %s (%s)", app->save_path,
                    state_status_string(save.status));
            log_add(&app->log, "Floor %d, HP %d, ATK %d, DEF %d, gold %d",
                    app->floor,
                    state_get(&app->state, ST_LIFE),
                    state_get(&app->state, ST_ATTACK),
                    state_get(&app->state, ST_DEFENSE),
                    state_get(&app->state, ST_GOLD));
        } else {
            log_add(&app->log, "Save rejected: %s", save_err);
        }
    }

    if (!loaded_save) {
        char map_path[APP_PATH_LEN];
        snprintf(map_path, sizeof(map_path), "%s/maps/maps.bin", data_dir);
        if (!load_map_file(app, map_path, err, errsz)) {
            cleanup_partial(app);
            return false;
        }
        log_add(&app->log, "Loaded floor %d map from %s", app->floor, map_path);
    } else {
        log_add(&app->log, "Loaded floor %d from save", app->floor);
    }

    log_add(&app->log, "Arrows move; F10 menu; Enter item; Esc close");

    char script[APP_PATH_LEN];
    if (find_smoke_script(data_dir, script, sizeof(script))) {
        char lua_err[256] = "";
        if (!events_init(&app->lua, script, event_log, app,
                         lua_err, sizeof(lua_err)))
            log_add(&app->log, "Lua smoke test failed: %s", lua_err);
    } else {
        log_add(&app->log, "Lua smoke script not found");
    }

    return true;
}

void app_run(App *app)
{
    if (!app) return;
    while (app->running) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            input_handle_event(app, &event);
        render_frame(app);
        SDL_Delay(1);
    }
}

void app_shutdown(App *app)
{
    if (!app) return;
    cleanup_partial(app);
    memset(app, 0, sizeof(*app));
}
