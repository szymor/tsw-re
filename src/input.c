#include "input.h"

#include "app.h"
#include "map.h"
#include "ui.h"

static void move_player(App *app, int dx, int dy)
{
    int nx = app->player_x + dx;
    int ny = app->player_y + dy;
    if (nx < 0) nx = 0;
    if (nx >= MAP_TILE_COLS) nx = MAP_TILE_COLS - 1;
    if (ny < 0) ny = 0;
    if (ny >= MAP_TILE_ROWS) ny = MAP_TILE_ROWS - 1;
    app->player_x = nx;
    app->player_y = ny;

    uint8_t raw = map_tile(&app->map, app->floor,
                           ny * MAP_TILE_COLS + nx);
    log_add(&app->log, "Moved to (%d,%d): %s (0x%02x)",
            nx, ny, map_tile_name(raw), raw);
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

    if (key == SDLK_F10) {
        if (repeated)
            return;
        ui_toggle_menu(&app->ui);
        log_add(&app->log, "F10: menu %s",
                app->ui.menu_open ? "opened" : "closed");
        return;
    }

    if (app->ui.menu_open) {
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
            if (app->ui.menu_selected == UI_MENU_QUIT) {
                app->running = false;
            } else {
                ui_close_menu(&app->ui);
                log_add(&app->log, "Menu: continue");
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
        return;
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
        log_add(&app->log, "Enter: item use/dialog confirmation is not implemented yet");
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
