#include "app.h"

#include "events.h"
#include "input.h"
#include "save.h"

#include <SDL3_image/SDL_image.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

Uint64 app_now(const App *app)
{
    if (app && app->clock_fn)
        return app->clock_fn();
    return SDL_GetTicks();
}

static void event_log(void *user, const char *message)
{
    App *app = user;
    if (app && message)
        log_add(&app->log, "%s", message);
}

static bool load_map_file(const char *path, MapBlock *out,
                          char *err, size_t errsz)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (err && errsz) snprintf(err, errsz, "cannot open %s", path);
        return false;
    }
    uint8_t data[MAP_BLOCK_SIZE + 1];
    size_t n = fread(data, 1, sizeof(data), f);
    fclose(f);
    return map_parse(data, n, out, err, errsz);
}

static bool find_builders_script(const char *data_dir, char *out, size_t outsz)
{
    char path[APP_PATH_BUF];
    snprintf(path, sizeof(path), "%s/events/builders.lua", data_dir);
    if (events_file_exists(path)) {
        snprintf(out, outsz, "%s", path);
        return true;
    }
#ifdef TSW_SOURCE_DIR
    snprintf(path, sizeof(path), "%s/src/events/builders.lua", TSW_SOURCE_DIR);
    if (events_file_exists(path)) {
        snprintf(out, outsz, "%s", path);
        return true;
    }
#endif
    snprintf(path, sizeof(path), "src/events/builders.lua");
    if (events_file_exists(path)) {
        snprintf(out, outsz, "%s", path);
        return true;
    }
    return false;
}

static void cleanup_partial(App *app)
{
    events_runtime_shutdown(&app->events);
    assets_shutdown(&app->assets);
    render_shutdown(&app->render);
    TTF_Quit();
    SDL_Quit();
}

bool app_save_slot_path(const App *app, int slot, char *out, size_t outsz)
{
    if (!app || !out || slot < 1 || slot > UI_SLOT_COUNT)
        return false;
    return snprintf(out, outsz, "%s/save%d.dat", app->save_dir, slot) < (int)outsz;
}

bool app_save_slot_exists(const App *app, int slot)
{
    char path[APP_PATH_BUF];
    if (!app_save_slot_path(app, slot, path, sizeof(path)))
        return false;
    struct stat st;
    if (stat(path, &st) == 0)
        return S_ISREG(st.st_mode);
    /* If metadata access fails for an existing-looking path, force the
     * overwrite prompt rather than silently treating the slot as empty. */
    return errno != ENOENT;
}

bool app_new_game(App *app, char *err, size_t errsz)
{
    if (!app) return false;
    if (!app->have_initial_map) {
        char path[APP_PATH_BUF];
        snprintf(path, sizeof(path), "%s/maps/initial.bin", app->data_dir);
        if (!load_map_file(path, &app->initial_map, err, errsz))
            return false;
        app->have_initial_map = true;
    }
    game_new(&app->game, &app->initial_map);
    events_cancel(&app->events);
    app->step_active = false;
    ui_close_menu(&app->ui);
    log_add(&app->log, "New game: floor 1, HP %d, ATK %d, DEF %d",
            state_get(&app->game.state, ST_LIFE),
            state_get(&app->game.state, ST_ATTACK),
            state_get(&app->game.state, ST_DEFENSE));
    return true;
}

bool app_save_slot(App *app, int slot, char *err, size_t errsz)
{
    if (!app) return false;
    if (state_get(&app->game.state, ST_LIFE) <= 0) {
        if (err && errsz) snprintf(err, errsz, "cannot save while dead");
        return false;
    }
    if (events_active(&app->events)) {
        if (err && errsz) snprintf(err, errsz, "cannot save during an event");
        return false;
    }
    char path[APP_PATH_BUF];
    if (!app_save_slot_path(app, slot, path, sizeof(path))) {
        if (err && errsz) snprintf(err, errsz, "invalid save slot %d", slot);
        return false;
    }
    if (!SDL_CreateDirectory(app->save_dir)) {
        if (err && errsz)
            snprintf(err, errsz, "cannot create save directory %s: %s",
                     app->save_dir, SDL_GetError());
        return false;
    }

    SaveData save;
    game_snapshot(&app->game, &save);
    if (!save_write_file(&save, path, err, errsz))
        return false;
    log_add(&app->log, "Saved slot %d to %s", slot, path);
    return true;
}

bool app_load_slot(App *app, int slot, char *err, size_t errsz)
{
    if (!app) return false;
    char path[APP_PATH_BUF];
    if (!app_save_slot_path(app, slot, path, sizeof(path))) {
        if (err && errsz) snprintf(err, errsz, "invalid save slot %d", slot);
        return false;
    }

    SaveData save;
    if (!save_load_file(path, &save, err, errsz))
        return false;
    game_load(&app->game, &save);
    events_cancel(&app->events);
    app->step_active = false;
    ui_close_menu(&app->ui);
    log_add(&app->log, "Loaded slot %d (%s): floor %d, HP %d",
            slot, state_status_string(save.status),
            game_floor(&app->game),
            state_get(&app->game.state, ST_LIFE));
    app_maybe_trigger_floor_event(app);
    return true;
}

bool app_checkpoint_save(App *app, char *err, size_t errsz)
{
    if (!app) return false;
    if (state_get(&app->game.state, ST_LIFE) <= 0) {
        if (err && errsz) snprintf(err, errsz, "cannot checkpoint while dead");
        return false;
    }
    char path[APP_PATH_BUF];
    int n = snprintf(path, sizeof(path), "%s/save0.dat", app->save_dir);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        if (err && errsz) snprintf(err, errsz, "checkpoint path is too long");
        return false;
    }
    if (!SDL_CreateDirectory(app->save_dir)) {
        if (err && errsz)
            snprintf(err, errsz, "cannot create save directory %s: %s",
                     app->save_dir, SDL_GetError());
        return false;
    }
    SaveData save;
    game_snapshot(&app->game, &save);
    if (!save_write_checkpoint_file(&save, path, err, errsz))
        return false;
    log_add(&app->log, "Checkpoint saved to %s", path);
    return true;
}

int64_t app_rank_score(App *app)
{
    if (!app) return 0;
    int64_t diff = (int64_t)state_get(&app->game.state, ST_OPTION_A) + 1;
    if (diff < 1) diff = 1;
    int64_t life = state_get(&app->game.state, ST_LIFE);
    int64_t attack = state_get(&app->game.state, ST_ATTACK);
    int64_t defense = state_get(&app->game.state, ST_DEFENSE);
    int64_t yellow = state_get(&app->game.state, ST_KEY_YELLOW);
    int64_t blue = state_get(&app->game.state, ST_KEY_BLUE);
    int64_t red = state_get(&app->game.state, ST_KEY_RED);
    int64_t score = 60 + (life + (diff + 1) * -400) / 50 +
                    attack * 3 + defense * 2 + yellow + blue * 2 + red * 3;
    if (score < 0) score = 0;
    if (score > INT32_MAX) score = INT32_MAX;
    return score;
}

void app_maybe_trigger_floor_event(App *app)
{
    if (!app || events_active(&app->events))
        return;
    if (state_get(&app->game.state, ST_LIFE) <= 0)
        return;
    /* No floor-entry events remain: floor 10 (the Golden Knight's room)
     * starts from its trigger tile (mevent -> ichicheck) and the fairy is
     * yousei, both on the command VM. */
}

bool app_init(App *app, const char *data_dir, const char *save_path,
              const char *save_dir, const char *font_path,
              char *err, size_t errsz)
{
    if (!app || !data_dir || !*data_dir) {
        if (err && errsz) snprintf(err, errsz, "missing data directory");
        return false;
    }
    if (strlen(data_dir) >= sizeof(app->data_dir) ||
        (save_dir && strlen(save_dir) >= sizeof(app->save_dir)) ||
        (save_path && strlen(save_path) >= sizeof(app->save_path)) ||
        (font_path && strlen(font_path) >= sizeof(app->font_path))) {
        if (err && errsz) snprintf(err, errsz, "path too long");
        return false;
    }

    memset(app, 0, sizeof(*app));
    app->clock_fn = SDL_GetTicks;
    snprintf(app->data_dir, sizeof(app->data_dir), "%s", data_dir);
    snprintf(app->save_dir, sizeof(app->save_dir), "%s",
             (save_dir && *save_dir) ? save_dir : ".");
    snprintf(app->save_path, sizeof(app->save_path), "%s",
             save_path ? save_path : "");
    snprintf(app->font_path, sizeof(app->font_path), "%s",
             font_path ? font_path : "");
    log_init(&app->log);
    ui_init(&app->ui);
    game_init(&app->game);
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

    char icon_path[APP_PATH_BUF];
    snprintf(icon_path, sizeof(icon_path), "%s/gfx/Icon.png", data_dir);
    SDL_Surface *icon = IMG_Load(icon_path);
    if (icon) {
        if (!SDL_SetWindowIcon(app->render.window, icon))
            log_add(&app->log, "Window icon failed: %s", SDL_GetError());
        SDL_DestroySurface(icon);
    }

    char map_path[APP_PATH_BUF];
    char map_err[256] = "";
    snprintf(map_path, sizeof(map_path), "%s/maps/initial.bin", data_dir);
    app->have_initial_map = load_map_file(map_path, &app->initial_map,
                                          map_err, sizeof(map_err));

    bool loaded_save = false;
    if (*app->save_path) {
        SaveData save;
        char save_err[256] = "";
        if (save_load_file(app->save_path, &save, save_err, sizeof(save_err))) {
            game_load(&app->game, &save);
            loaded_save = true;
            log_add(&app->log, "Save: %s (%s)", app->save_path,
                    state_status_string(save.status));
            log_add(&app->log, "Floor %d, HP %d, ATK %d, DEF %d, gold %d",
                    game_floor(&app->game),
                    state_get(&app->game.state, ST_LIFE),
                    state_get(&app->game.state, ST_ATTACK),
                    state_get(&app->game.state, ST_DEFENSE),
                    state_get(&app->game.state, ST_GOLD));
        } else {
            log_add(&app->log, "Save rejected: %s", save_err);
        }
    }

    if (!loaded_save) {
        if (!app->have_initial_map) {
            if (err && errsz)
                snprintf(err, errsz, "%s", map_err);
            cleanup_partial(app);
            return false;
        }
        game_new(&app->game, &app->initial_map);
        log_add(&app->log, "New game: floor 1, HP %d, ATK %d, DEF %d",
                state_get(&app->game.state, ST_LIFE),
                state_get(&app->game.state, ST_ATTACK),
                state_get(&app->game.state, ST_DEFENSE));
    } else {
        log_add(&app->log, "Loaded floor %d from save", game_floor(&app->game));
    }

    log_add(&app->log, "Arrows move; F10 menu; F2 new; F9 quit; Enter item");

    char script[APP_PATH_BUF];
    if (!find_builders_script(data_dir, script, sizeof(script))) {
        if (err && errsz)
            snprintf(err, errsz,
                     "event builders not found; expected events/builders.lua");
        cleanup_partial(app);
        return false;
    }
    char lua_err[256] = "";
    if (!events_runtime_init(&app->events, app, script, event_log, app,
                             lua_err, sizeof(lua_err))) {
        if (err && errsz)
            snprintf(err, errsz, "event runtime failed: %s", lua_err);
        cleanup_partial(app);
        return false;
    }

    app_maybe_trigger_floor_event(app);
    return true;
}

void app_run(App *app)
{
    if (!app) return;
    while (app->running) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            input_handle_event(app, &event);
        events_update(&app->events, app_now(app));
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
