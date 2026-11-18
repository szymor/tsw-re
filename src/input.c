#include "input.h"
#include "itext.h"

#include "app.h"
#include "battle.h"
#include "game.h"
#include "event_engine.h"
#include "items.h"
#include "ui.h"

#include <stdio.h>

static void new_game(App *app)
{
    char err[256] = "";
    if (!app_new_game(app, err, sizeof(err)))
        log_add(&app->log, tx(ITX_LOG_NEW_GAME_FAILED), err);
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

static void use_inventory_item(App *app)
{
    int slot = app->ui.inventory_selected;
    if (state_get(&app->game.state, ST_SPECIAL_FIRST + slot) == 0)
        return;
    /* formkeydown Enter on the overlay: img6work .. img17work. */
    int result = events_use_item(&app->events, slot);
    if (result == EVENT_ENGINE_NOT_ROUTED) {
        log_add(&app->log, tx(ITX_LOG_ITEM_PASSIVE), special_item_name(slot));
        return;
    }
    if (result != EVENT_ENGINE_STARTED)
        return;
    ui_close_menu(&app->ui);            /* itemdel */
    EventEngine *e = &app->events.engine;
    if (e->hero_orb_pending) {
        e->hero_orb_pending = false;
        app->forecast_count = 0;
        for (int i = 0; i < e->hero_rows && i < FORECAST_LINES; i++)
            battle_hero_row_text(&app->game, e->hero_row_index[i],
                                 e->hero_row_damage[i],
                                 app->forecast[app->forecast_count++],
                                 FORECAST_LINE_LEN);
        if (app->forecast_count == 0) {
            snprintf(app->forecast[app->forecast_count++], FORECAST_LINE_LEN,
                     "No normal monsters on this floor.");
        }
        ui_open_forecast(&app->ui);
    }
    e->wisdom_orb_pending = false;      /* the engine's window (wisdom_open) */
    /* The Orb of Wisdom's list shows the highlighted man's words at once. */
    if (e->wisdom_open && e->wisdom_focus >= 0)
        event_engine_wisdom_press(e, e->wisdom_focus);
}

static bool save_slot(App *app, int slot)
{
    char err[256] = "";
    if (!app_save_slot(app, slot, err, sizeof(err))) {
        log_add(&app->log, tx(ITX_LOG_SAVE_FAILED), slot, err);
        return false;
    }
    return true;
}

static void load_slot(App *app, int slot)
{
    char err[256] = "";
    if (!app_load_slot(app, slot, err, sizeof(err)))
        log_add(&app->log, tx(ITX_LOG_LOAD_FAILED), slot, err);
}

static bool is_arrow(SDL_Keycode key)
{
    return key == SDLK_UP || key == SDLK_DOWN || key == SDLK_LEFT ||
           key == SDLK_RIGHT;
}

static Uint64 walk_interval(const App *app)
{
    return (Uint64)event_vm_speed_interval_ms(app->options.speed);
}

static void walk_step(App *app, SDL_Keycode key)
{
    switch (key) {
    case SDLK_UP:    move_player(app, 0, -1); break;
    case SDLK_DOWN:  move_player(app, 0, 1); break;
    case SDLK_LEFT:  move_player(app, -1, 0); break;
    case SDLK_RIGHT: move_player(app, 1, 0); break;
    default: break;
    }
}

/* The released arrow stops the walk, unless another one is still held:
 * that one takes over. */
static void walk_release(App *app, SDL_Keycode key)
{
    if (key != app->walk_key)
        return;
    app->walk_key = 0;
    const bool *down = SDL_GetKeyboardState(NULL);
    static const SDL_Keycode ARROWS[4] = {
        SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT
    };
    for (int i = 0; down && i < 4; i++) {
        SDL_Scancode sc = SDL_GetScancodeFromKey(ARROWS[i], NULL);
        if (ARROWS[i] != key && sc != SDL_SCANCODE_UNKNOWN && down[sc]) {
            app->walk_key = ARROWS[i];
            break;
        }
    }
}

void input_update(App *app)
{
    if (!app || !app->walk_key)
        return;
    if (app->intro.active || app->ui.view != UI_VIEW_GAME)
        return;
    if (events_active(&app->events))
        return;                         /* goes on once the event is over */
    Uint64 now = app_now(app);
    if (now < app->next_walk_ms)
        return;
    app->next_walk_ms = now + walk_interval(app);
    walk_step(app, app->walk_key);
}

static bool can_save(const App *app)
{
    return state_get(&app->game.state, ST_LIFE) > 0;
}

static void menu_enter(App *app)
{
    UiState *ui = &app->ui;
    if (ui->menu_tab == UI_TAB_GAME) {
        switch (ui->menu_selected) {
        case UI_GAME_NEW:
            ui_open_new_confirm(ui);
            break;
        case UI_GAME_LOAD: {
            app_refresh_slots(app);
            ui_open_load(ui, app->slot_info[0][0] != '\0');
            break;
        }
        case UI_GAME_SAVE:
            if (can_save(app)) {
                app_refresh_slots(app);
                ui_open_save(ui);
            }
            break;
        case UI_GAME_RANKING:
            app_open_ranking(app);
            break;
        case UI_GAME_QUIT:
            app->running = false;
            break;
        default:
            break;
        }
    } else if (ui->menu_tab == UI_TAB_OPTIONS) {
        Options *o = &app->options;
        switch (ui->menu_selected) {
        case UI_OPT_SOUND:
            app_set_sound(app, !o->sound);
            break;
        case UI_OPT_MUSIC:
            app_set_music(app, !o->music);
            break;
        case UI_OPT_SPEED:           /* High -> Middle -> Low -> High */
            app_set_speed(app, o->speed % 3 + 1);
            break;
        case UI_OPT_TEXT:            /* Tiny -> Small -> Normal -> Big */
            app_set_text_size(app,
                              (OptTextSize)((o->text + 1) % OPT_TEXT_COUNT));
            break;
        case UI_OPT_LANGUAGE:        /* the data/text/ folders, in turn */
            app_next_language(app);
            break;
        default:
            break;
        }
    } else if (ui->menu_tab == UI_TAB_HELP) {
        ui_open_view(ui, ui->menu_selected == UI_HELP_CONTROLS
                             ? UI_VIEW_CONTROLS : UI_VIEW_ABOUT);
    }
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
    case SDLK_LEFT:
        ui_move_tab(&app->ui, -1);
        break;
    case SDLK_RIGHT:
    case SDLK_TAB:
        ui_move_tab(&app->ui, 1);
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE:
        if (!repeated)
            menu_enter(app);
        break;
    case SDLK_ESCAPE:
    case SDLK_F10:
        if (!repeated)
            ui_close_menu(&app->ui);
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
        int slot = ui_load_row_slot(&app->ui, app->ui.slot_selected);
        if (app->slot_info[slot][0])
            load_slot(app, slot);
    }
}

/* A Yes/No confirmation: Left/Right/Tab move, Y/N answer, Enter takes
 * the highlighted answer, Escape is No.  Returns 1 yes, 0 no, -1 none. */
static int confirm_key(App *app, SDL_Keycode key, bool repeated)
{
    UiState *ui = &app->ui;
    if (key == SDLK_LEFT) { ui->confirm_yes = true; return -1; }
    if (key == SDLK_RIGHT) { ui->confirm_yes = false; return -1; }
    if (key == SDLK_TAB) { ui->confirm_yes = !ui->confirm_yes; return -1; }
    if (repeated)
        return -1;
    if (key == (SDL_Keycode)itext_keycode(ITX_KEY_YES))
        return 1;
    if (key == (SDL_Keycode)itext_keycode(ITX_KEY_NO) || key == SDLK_ESCAPE)
        return 0;
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE)
        return ui->confirm_yes ? 1 : 0;
    return -1;
}

static void handle_new_confirm(App *app, SDL_Keycode key, bool repeated)
{
    int a = confirm_key(app, key, repeated);
    if (a == 1)
        new_game(app);
    else if (a == 0)
        ui_back(&app->ui);
}

static void handle_overwrite(App *app, SDL_Keycode key, bool repeated)
{
    int a = confirm_key(app, key, repeated);
    if (a == 0) {
        ui_back(&app->ui);
    } else if (a == 1) {
        int slot = app->ui.overwrite_slot + 1;
        if (save_slot(app, slot))
            ui_close_menu(&app->ui);
    }
}

void input_handle_event(struct App *app, const SDL_Event *event)
{
    if (!app || !event)
        return;

    if (event->type == SDL_EVENT_QUIT) {
        app->running = false;
        return;
    }
    if (event->type == SDL_EVENT_TEXT_INPUT) {
        events_handle_text(&app->events, event->text.text);
        return;
    }
    if (event->type == SDL_EVENT_TEXT_EDITING) {
        events_handle_editing(&app->events, event->edit.text);
        return;
    }
    if (event->type == SDL_EVENT_KEY_UP) {
        if (is_arrow(event->key.key))
            walk_release(app, event->key.key);
        return;
    }
    if (event->type != SDL_EVENT_KEY_DOWN)
        return;

    SDL_Keycode key = event->key.key;
    bool repeated = event->key.repeat;

    /* F11 / Alt+Enter: fullscreen, anywhere (also in the intro and in a
     * window, where Enter alone confirms). */
    if (key == SDLK_F11 ||
        ((key == SDLK_RETURN || key == SDLK_KP_ENTER) &&
         (event->key.mod & SDL_KMOD_ALT))) {
        if (!repeated)
            app_toggle_fullscreen(app);
        return;
    }
    if (app->intro.active)
        return;
    if (key == SDLK_PAGEUP || key == SDLK_PAGEDOWN) {
        log_scroll(&app->log, key == SDLK_PAGEUP ? LOG_VISIBLE : -LOG_VISIBLE);
        return;
    }
    if (events_active(&app->events)) {
        events_handle_key(&app->events, key, repeated);
        return;
    }

    /* F10, or Escape with no window open, opens the menu bar. */
    if (app->ui.view == UI_VIEW_GAME &&
        (key == SDLK_F10 || key == SDLK_ESCAPE)) {
        if (!repeated)
            ui_toggle_menu(&app->ui);
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
    case UI_VIEW_CONTROLS:
    case UI_VIEW_ABOUT:
        if (!repeated && (key == SDLK_ESCAPE || key == SDLK_RETURN ||
                          key == SDLK_KP_ENTER || key == SDLK_SPACE))
            ui_back(&app->ui);
        return;
    case UI_VIEW_FORECAST:
        if (key == SDLK_ESCAPE || key == SDLK_RETURN ||
            key == SDLK_KP_ENTER) {
            if (!repeated) {
                ui_close_menu(&app->ui);
                /* Button36Click (0x4503ec) closes the window, Timer1 on. */
                app->events.engine.timer1 = true;
            }
        }
        return;
    case UI_VIEW_GAME:
    default:
        break;
    }

    switch (key) {
    case SDLK_UP:
    case SDLK_DOWN:
    case SDLK_LEFT:
    case SDLK_RIGHT:
        /* A press steps at once; holding it walks on (input_update).  The
         * keyboard's own repeats are ignored. */
        if (repeated)
            break;
        app->walk_key = key;
        app->next_walk_ms = app_now(app) + walk_interval(app);
        walk_step(app, key);
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (repeated)
            break;
        ui_open_inventory(&app->ui);
        log_add(&app->log, "%s", tx(ITX_LOG_INVENTORY));
        break;
    default:
        break;
    }
}
