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

static void move_player(App *app, int dx, int dy)
{
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

    char msg[256] = "";
    GameMoveResult result = game_move(&app->game, dx, dy, msg, sizeof(msg));
    if (result == GAME_MOVE_OK ||
        result == GAME_MOVE_DOOR_OPENED ||
        result == GAME_MOVE_KEY_PICKUP ||
        result == GAME_MOVE_ITEM_PICKUP ||
        result == GAME_MOVE_BATTLE_WIN) {
        app->step_active = true;
        app->step_from_x = from_x;
        app->step_from_y = from_y;
        app->step_start_ms = app_now(app);
    } else if (result == GAME_MOVE_STAIRS_UP ||
               result == GAME_MOVE_STAIRS_DOWN ||
               result == GAME_MOVE_DEAD) {
        app->step_active = false;
    }
    /* Blocked moves keep the current slide running; only facing changes. */
    if (result == GAME_MOVE_EVENT) {
        app->step_active = false;
        if (!events_trigger(&app->events,
                            app->game.event_floor,
                            app->game.event_index,
                            app->game.event_raw)) {
            log_add(&app->log, "Nothing happens here.");
        }
        return;
    }
    if (result == GAME_MOVE_STAIRS_UP ||
        result == GAME_MOVE_STAIRS_DOWN) {
        app->step_active = false;
        app_maybe_trigger_floor_event(app);
    }
    if (msg[0])
        log_add(&app->log, "%s", msg);
    if (result == GAME_MOVE_BATTLE_WIN)
        events_after_battle(&app->events);   /* moncheck (splice 0x0b/5) */
    if (result == GAME_MOVE_DEAD)
        ui_open_game_over(&app->ui);
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
