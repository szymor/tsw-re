#include "app.h"
#include "game.h"
#include "input.h"
#include "map.h"
#include "state.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static void key_event(App *app, SDL_Keycode code, bool repeat)
{
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.key = code;
    ev.key.repeat = repeat;
    input_handle_event(app, &ev);
}

static void key(App *app, SDL_Keycode code)
{
    key_event(app, code, false);
}

static void place(App *app, int floor, int x, int y)
{
    state_set(&app->game.state, ST_FLOOR, floor);
    state_set(&app->game.state, ST_X, x);
    state_set(&app->game.state, ST_Y, y);
}

static Uint64 fake_now_ms = 1000;

static Uint64 fake_clock(void)
{
    return fake_now_ms;
}

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void)
{
    MapBlock map;
    memset(&map, 0, sizeof(map));
    map.bytes[0] = 0x00;
    map.bytes[1] = 0x01;
    int off = map_floor_offset(1);
    memset(map.bytes + off, 0x06, MAP_TILES);

    App app;
    memset(&app, 0, sizeof(app));
    app.clock_fn = fake_clock;
    log_init(&app.log);
    ui_init(&app.ui);
    game_new(&app.game, &map);
    app.initial_map = map;
    app.have_initial_map = true;
    app.running = true;
    snprintf(app.save_dir, sizeof(app.save_dir), ".");
    place(&app, 1, 5, 5);

    key(&app, SDLK_UP);
    CHECK(game_y(&app.game) == 4);
    CHECK(app.step_active);
    CHECK(app.step_from_x == 5 && app.step_from_y == 5);
    key(&app, SDLK_LEFT);
    CHECK(game_x(&app.game) == 4);

    app.step_active = false;
    place(&app, 1, 0, 0);
    key(&app, SDLK_LEFT);
    key(&app, SDLK_UP);
    CHECK(game_x(&app.game) == 0 && game_y(&app.game) == 0);
    CHECK(!app.step_active);

    /* A new move rebases the slide from the current interpolated position. */
    place(&app, 1, 5, 5);
    app.step_active = true;
    app.step_from_x = 4.0f;
    app.step_from_y = 5.0f;
    app.step_start_ms = fake_now_ms - 10;
    key(&app, SDLK_RIGHT);
    CHECK(game_x(&app.game) == 6);
    CHECK(app.step_active);
    CHECK(app.step_from_x > 4.0f && app.step_from_x < 5.0f);

    /* Blocked input during a slide preserves the slide. */
    float keep_from = app.step_from_x;
    app.game.map.bytes[map_floor_offset(1) + 5 * MAP_TILE_COLS + 5] = 0x08;
    key(&app, SDLK_LEFT);
    CHECK(game_x(&app.game) == 6);
    CHECK(app.step_active);
    CHECK(app.step_from_x == keep_from);

    /* Inventory opens from gameplay; slot 12 uses the Magic Key. */
    place(&app, 1, 5, 5);
    app.game.map.bytes[map_floor_offset(1) + 10] = 0x01;
    state_set(&app.game.state, ST_SPECIAL_FIRST + 12, 1);
    key(&app, SDLK_RETURN);
    CHECK(app.ui.view == UI_VIEW_INVENTORY);
    key(&app, SDLK_DOWN);
    key(&app, SDLK_DOWN);
    key(&app, SDLK_DOWN);
    key(&app, SDLK_DOWN);
    CHECK(app.ui.inventory_selected == 12);
    key(&app, SDLK_RETURN);
    CHECK(app.ui.view == UI_VIEW_GAME);
    CHECK(app.game.map.bytes[map_floor_offset(1) + 10] == 0x06);
    CHECK(state_get(&app.game.state, ST_SPECIAL_FIRST + 12) == 0);

    /* A position-changing item clears any active slide. */
    place(&app, 1, 5, 5);
    app.game.map.bytes[map_floor_offset(1) + 50] = 0x0b;
    state_set(&app.game.state, ST_SPECIAL_FIRST + 7, 3);
    app.step_active = true;
    app.step_from_x = 4.0f;
    app.step_from_y = 5.0f;
    app.step_start_ms = fake_now_ms;
    ui_open_inventory(&app.ui);
    app.ui.inventory_selected = 7;
    key(&app, SDLK_RETURN);
    CHECK(app.ui.view == UI_VIEW_GAME);
    CHECK(!app.step_active);
    CHECK(game_x(&app.game) == 50 % MAP_TILE_COLS);
    CHECK(game_y(&app.game) == 50 / MAP_TILE_COLS);

    /* Game-over overlay cannot be dismissed into dead gameplay. */
    state_set(&app.game.state, ST_LIFE, 0);
    ui_open_game_over(&app.ui);
    key(&app, SDLK_F2);
    CHECK(app.ui.view == UI_VIEW_NEW_CONFIRM);
    key(&app, SDLK_F10);
    CHECK(app.ui.view == UI_VIEW_NEW_CONFIRM);
    key(&app, SDLK_ESCAPE);
    CHECK(app.ui.view == UI_VIEW_GAME_OVER);
    key(&app, SDLK_F10);
    CHECK(app.ui.view == UI_VIEW_GAME_OVER);
    snprintf(app.save_dir, sizeof(app.save_dir), "test-save-dir");
    char dead_err[128] = "";
    CHECK(!app_save_slot(&app, 1, dead_err, sizeof(dead_err)));
    CHECK(strstr(dead_err, "dead") != NULL);
    key(&app, SDLK_RETURN);
    CHECK(app.ui.view == UI_VIEW_GAME);
    CHECK(game_stat(&app.game, ST_LIFE) == 1000);

    /* New game asks for confirmation, then resets position and stats. */
    place(&app, 3, 2, 2);
    state_set(&app.game.state, ST_LIFE, 1);
    key(&app, SDLK_F2);
    CHECK(app.ui.view == UI_VIEW_NEW_CONFIRM);
    key(&app, SDLK_F2);
    CHECK(app.ui.view == UI_VIEW_NEW_CONFIRM);
    key(&app, SDLK_ESCAPE);
    CHECK(app.ui.view == UI_VIEW_GAME);
    CHECK(game_floor(&app.game) == 3);
    key(&app, SDLK_F2);
    CHECK(app.ui.view == UI_VIEW_NEW_CONFIRM);
    key(&app, SDLK_RETURN);
    CHECK(game_floor(&app.game) == 1);
    CHECK(game_x(&app.game) == 5 && game_y(&app.game) == 10);
    CHECK(game_stat(&app.game, ST_LIFE) == 1000);

    key(&app, SDLK_F10);
    key(&app, SDLK_DOWN);
    key(&app, SDLK_RETURN);
    CHECK(app.ui.view == UI_VIEW_NEW_CONFIRM);
    key(&app, SDLK_ESCAPE);
    CHECK(app.ui.view == UI_VIEW_MENU);
    key(&app, SDLK_ESCAPE);
    CHECK(app.ui.view == UI_VIEW_GAME);

    key(&app, SDLK_F10);
    CHECK(app.ui.view == UI_VIEW_MENU);
    key_event(&app, SDLK_F10, true);
    CHECK(app.ui.view == UI_VIEW_MENU);
    key_event(&app, SDLK_F10, false);
    CHECK(app.ui.view == UI_VIEW_GAME);

    key(&app, SDLK_F10);
    CHECK(app.ui.view == UI_VIEW_MENU);
    key(&app, SDLK_DOWN);
    CHECK(app.ui.menu_selected == UI_MENU_NEW_GAME);
    key(&app, SDLK_DOWN);
    CHECK(app.ui.menu_selected == UI_MENU_SAVE);
    key(&app, SDLK_DOWN);
    CHECK(app.ui.menu_selected == UI_MENU_LOAD);
    key(&app, SDLK_DOWN);
    CHECK(app.ui.menu_selected == UI_MENU_QUIT);
    key(&app, SDLK_RETURN);
    CHECK(!app.running);

    /* Save/load submenus and back-navigation. */
    key(&app, SDLK_ESCAPE);
    CHECK(app.ui.view == UI_VIEW_GAME);
    app.running = true;
    key(&app, SDLK_F10);
    key(&app, SDLK_DOWN);
    key(&app, SDLK_DOWN);
    key(&app, SDLK_RETURN);
    CHECK(app.ui.view == UI_VIEW_SAVE);
    key(&app, SDLK_ESCAPE);
    CHECK(app.ui.view == UI_VIEW_MENU);
    key(&app, SDLK_ESCAPE);
    CHECK(app.ui.view == UI_VIEW_GAME);

    key(&app, SDLK_F10);
    key(&app, SDLK_DOWN);
    key(&app, SDLK_DOWN);
    key(&app, SDLK_DOWN);
    key(&app, SDLK_RETURN);
    CHECK(app.ui.view == UI_VIEW_LOAD);
    key(&app, SDLK_ESCAPE);
    CHECK(app.ui.view == UI_VIEW_MENU);
    key(&app, SDLK_ESCAPE);
    CHECK(app.ui.view == UI_VIEW_GAME);

    key(&app, SDLK_RETURN);
    CHECK(app.running);

    /* Real save-slot write/load through the app helpers. */
    snprintf(app.save_dir, sizeof(app.save_dir), "test-save-dir");
    place(&app, 4, 7, 8);
    state_set(&app.game.state, ST_LIFE, 4321);
    char err[256] = "";
    CHECK(app_save_slot(&app, 2, err, sizeof(err)));
    CHECK(app_save_slot_exists(&app, 2));
    CHECK(chmod("test-save-dir/save2.dat", 0200) == 0);
    CHECK(app_save_slot_exists(&app, 2));
    CHECK(chmod("test-save-dir/save2.dat", 0644) == 0);
    place(&app, 1, 0, 0);
    state_set(&app.game.state, ST_LIFE, 1);
    CHECK(app_load_slot(&app, 2, err, sizeof(err)));
    CHECK(game_floor(&app.game) == 4);
    CHECK(game_x(&app.game) == 7 && game_y(&app.game) == 8);
    CHECK(game_stat(&app.game, ST_LIFE) == 4321);

    /* Existing slot prompts before overwriting. */
    ui_open_save(&app.ui);
    app.ui.slot_selected = 1; /* save2.dat */
    key(&app, SDLK_RETURN);
    CHECK(app.ui.view == UI_VIEW_OVERWRITE);
    key(&app, SDLK_ESCAPE);
    CHECK(app.ui.view == UI_VIEW_SAVE);
    key(&app, SDLK_RETURN);
    CHECK(app.ui.view == UI_VIEW_OVERWRITE);
    key(&app, SDLK_RETURN);
    CHECK(app.ui.view == UI_VIEW_GAME);

    remove("test-save-dir/save2.dat");
    remove("test-save-dir");

    printf("test_input: ok\n");
    return 0;
}
