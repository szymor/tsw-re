#include "input.h"

#include "app.h"
#include "battle.h"
#include "game.h"
#include "items.h"
#include "ui.h"

#include <stdio.h>

static void new_game(App *app)
{
    char err[256] = "";
    if (!app_new_game(app, err, sizeof(err)))
        log_add(&app->log, "New game failed: %s", err);
}

/* formkeydown (0x460bd8): an arrow turns the hero, then, unless he faces
 * the edge of the floor, handan (0x444490) acts on the cell in front of
 * him -- walking, doors, items, battles, stairs and events all run as
 * builders on the command VM. */
static void move_player(App *app, int dx, int dy)
{
    if (events_active(&app->events))
        return;                         /* g_tl_count != 0: keys ignored */
    float from_x = (float)game_x(&app->game);
    float from_y = (float)game_y(&app->game);
    if (app->step_active) {
        Uint64 now = app_now(app);
        float t = (float)(now - app->step_start_ms) / 90.0f;
        if (t < 1.0f) {
            from_x = app->step_from_x +
                     ((float)game_x(&app->game) - app->step_from_x) * t;
            from_y = app->step_from_y +
                     ((float)game_y(&app->game) - app->step_from_y) * t;
        }
    }

    int facing = dy > 0 ? 1 : dx < 0 ? 2 : dx > 0 ? 3 : 4;
    state_set(&app->game.state, ST_FACING, facing);
    state_compute_checksums(&app->game.state);
    int x = game_x(&app->game), y = game_y(&app->game);
    int nx = x + dx, ny = y + dy;
    if (nx < 0 || nx >= MAP_TILE_COLS || ny < 0 || ny >= MAP_TILE_ROWS)
        return;
    int floor = game_floor(&app->game);
    int index = ny * MAP_TILE_COLS + nx;
    uint8_t raw = map_tile(&app->game.map, floor, index);
    if (!events_trigger(&app->events, floor, index, raw))
        return;
    /* A move rebases the slide from where the hero is drawn now. */
    if (app->step_active &&
        (game_x(&app->game) != x || game_y(&app->game) != y)) {
        app->step_from_x = from_x;
        app->step_from_y = from_y;
    }
    if (game_floor(&app->game) != floor)
        app->step_active = false;       /* stairs */
}

static void forecast_store(void *user, const char *line)
{
    App *app = user;
    if (app->forecast_count < FORECAST_LINES)
        snprintf(app->forecast[app->forecast_count++],
                 FORECAST_LINE_LEN, "%s", line);
}

static void use_inventory_item(App *app)
{
    int slot = app->ui.inventory_selected;
    char msg[256] = "";
    ItemUseResult result = item_use_special(&app->game, slot, msg, sizeof(msg));
    if (result == ITEM_USE_FORECAST) {
        app->forecast_count = 0;
        battle_forecast_floor(&app->game, forecast_store, app);
        if (app->forecast_count == 0) {
            snprintf(app->forecast[app->forecast_count++], FORECAST_LINE_LEN,
                     "No normal monsters on this floor.");
        }
        ui_open_forecast(&app->ui);
        return;
    }
    if (msg[0])
        log_add(&app->log, "%s", msg);
    if (result == ITEM_USE_OK) {
        app->step_active = false;
        ui_close_menu(&app->ui);
        app_maybe_trigger_floor_event(app);
    }
}

static bool save_slot(App *app, int slot)
{
    char err[256] = "";
    if (!app_save_slot(app, slot, err, sizeof(err))) {
        log_add(&app->log, "Save slot %d failed: %s", slot, err);
        return false;
    }
    return true;
}

static void load_slot(App *app, int slot)
{
    char err[256] = "";
    if (!app_load_slot(app, slot, err, sizeof(err)))
        log_add(&app->log, "Load slot %d failed: %s", slot, err);
}

static void handle_menu(App *app, SDL_Keycode key, bool repeated)
{
    switch (key) {
    case SDLK_UP:
        ui_move_selection(&app->ui, -1);
        break;
    case SDLK_DOWN:
        ui_move_selection(&app->ui, 1);
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (repeated)
            break;
        switch (app->ui.menu_selected) {
        case UI_MENU_CONTINUE:
            ui_close_menu(&app->ui);
            log_add(&app->log, "Menu: continue");
            break;
        case UI_MENU_NEW_GAME:
            ui_open_new_confirm(&app->ui);
            break;
        case UI_MENU_SAVE:
            ui_open_save(&app->ui);
            break;
        case UI_MENU_LOAD:
            ui_open_load(&app->ui);
            break;
        case UI_MENU_QUIT:
            app->running = false;
            break;
        default:
            break;
        }
        break;
    case SDLK_ESCAPE:
        if (repeated)
            break;
        ui_close_menu(&app->ui);
        log_add(&app->log, "Menu: closed");
        break;
    default:
        break;
    }
}

static void handle_save_load(App *app, SDL_Keycode key, bool repeated)
{
    if (key == SDLK_ESCAPE) {
        if (!repeated)
            ui_back(&app->ui);
        return;
    }
    if (key == SDLK_UP) {
        ui_move_selection(&app->ui, -1);
        return;
    }
    if (key == SDLK_DOWN) {
        ui_move_selection(&app->ui, 1);
        return;
    }
    if (key != SDLK_RETURN && key != SDLK_KP_ENTER)
        return;
    if (repeated)
        return;

    if (app->ui.view == UI_VIEW_SAVE) {
        int slot = app->ui.slot_selected + 1;
        if (app_save_slot_exists(app, slot)) {
            ui_begin_overwrite(&app->ui, app->ui.slot_selected);
        } else if (save_slot(app, slot)) {
            ui_close_menu(&app->ui);
        }
    } else if (app->ui.view == UI_VIEW_LOAD) {
        load_slot(app, app->ui.slot_selected + 1);
    }
}

static void handle_new_confirm(App *app, SDL_Keycode key, bool repeated)
{
    if (key == SDLK_ESCAPE) {
        if (!repeated)
            ui_back(&app->ui);
        return;
    }
    if (key != SDLK_RETURN && key != SDLK_KP_ENTER)
        return;
    if (repeated)
        return;
    new_game(app);
}

static void handle_overwrite(App *app, SDL_Keycode key, bool repeated)
{
    if (key == SDLK_ESCAPE) {
        if (!repeated)
            ui_back(&app->ui);
        return;
    }
    if (key != SDLK_RETURN && key != SDLK_KP_ENTER)
        return;
    if (repeated)
        return;
    int slot = app->ui.overwrite_slot + 1;
    if (save_slot(app, slot))
        ui_close_menu(&app->ui);
}

void input_handle_event(struct App *app, const SDL_Event *event)
{
    if (!app || !event)
        return;

    if (event->type == SDL_EVENT_QUIT) {
        app->running = false;
        return;
    }
    if (event->type != SDL_EVENT_KEY_DOWN)
        return;

    SDL_Keycode key = event->key.key;
    bool repeated = event->key.repeat;

    if (key == SDLK_F9) {
        if (!repeated)
            app->running = false;
        return;
    }
    if (app->intro.active)
        return;
    if (events_active(&app->events)) {
        events_handle_key(&app->events, key, repeated);
        return;
    }

    if (key == SDLK_F10) {
        if (repeated || app->ui.view == UI_VIEW_GAME_OVER ||
            (app->ui.view == UI_VIEW_NEW_CONFIRM &&
             app->ui.new_confirm_back == UI_VIEW_GAME_OVER))
            return;
        ui_toggle_menu(&app->ui);
        log_add(&app->log, "F10: menu %s",
                app->ui.view == UI_VIEW_GAME ? "closed" : "opened");
        return;
    }
    if (key == SDLK_F2) {
        if (!repeated)
            ui_open_new_confirm(&app->ui);
        return;
    }
    switch (app->ui.view) {
    case UI_VIEW_MENU:
        handle_menu(app, key, repeated);
        return;
    case UI_VIEW_SAVE:
    case UI_VIEW_LOAD:
        handle_save_load(app, key, repeated);
        return;
    case UI_VIEW_OVERWRITE:
        handle_overwrite(app, key, repeated);
        return;
    case UI_VIEW_NEW_CONFIRM:
        handle_new_confirm(app, key, repeated);
        return;
    case UI_VIEW_INVENTORY:
        if (key == SDLK_ESCAPE) {
            if (!repeated)
                ui_close_menu(&app->ui);
            return;
        }
        if (key == SDLK_UP) { ui_inventory_move(&app->ui, 0, -1); return; }
        if (key == SDLK_DOWN) { ui_inventory_move(&app->ui, 0, 1); return; }
        if (key == SDLK_LEFT) { ui_inventory_move(&app->ui, -1, 0); return; }
        if (key == SDLK_RIGHT) { ui_inventory_move(&app->ui, 1, 0); return; }
        if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && !repeated)
            use_inventory_item(app);
        return;
    case UI_VIEW_GAME_OVER:
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (!repeated)
                new_game(app);
        } else if (key == SDLK_ESCAPE) {
            if (!repeated)
                app->running = false;
        }
        return;
    case UI_VIEW_FORECAST:
        if (key == SDLK_ESCAPE || key == SDLK_RETURN ||
            key == SDLK_KP_ENTER) {
            if (!repeated)
                ui_close_menu(&app->ui);
        }
        return;
    case UI_VIEW_GAME:
    default:
        break;
    }

    switch (key) {
    case SDLK_UP:    move_player(app, 0, -1); break;
    case SDLK_DOWN:  move_player(app, 0, 1); break;
    case SDLK_LEFT:  move_player(app, -1, 0); break;
    case SDLK_RIGHT: move_player(app, 1, 0); break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (repeated)
            break;
        ui_open_inventory(&app->ui);
        log_add(&app->log, "Inventory opened.");
        break;
    case SDLK_ESCAPE:
        if (repeated)
            break;
        log_add(&app->log, "Esc: no active dialog");
        break;
    default:
        break;
    }
}
