#include "app.h"
#include "input.h"
#include "map.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>

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

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void)
{
    App app;
    memset(&app, 0, sizeof(app));
    log_init(&app.log);
    ui_init(&app.ui);
    app.running = true;
    app.floor = 1;
    app.player_x = 5;
    app.player_y = 5;
    app.map.bytes[0] = 0x00;
    app.map.bytes[1] = 0x01;
    int off = map_floor_offset(1);
    for (int i = 0; i < MAP_TILES; i++)
        app.map.bytes[off + i] = 0x06;

    key(&app, SDLK_UP);
    CHECK(app.player_y == 4);
    key(&app, SDLK_LEFT);
    CHECK(app.player_x == 4);
    CHECK(app.log.count >= 2);

    app.player_x = 0;
    app.player_y = 0;
    key(&app, SDLK_LEFT);
    key(&app, SDLK_UP);
    CHECK(app.player_x == 0 && app.player_y == 0);

    key(&app, SDLK_F10);
    CHECK(app.ui.menu_open);
    key_event(&app, SDLK_F10, true);
    CHECK(app.ui.menu_open);
    key_event(&app, SDLK_F10, false);
    CHECK(!app.ui.menu_open);
    key(&app, SDLK_F10);
    CHECK(app.ui.menu_open);
    key(&app, SDLK_DOWN);
    CHECK(app.ui.menu_selected == UI_MENU_QUIT);
    key(&app, SDLK_RETURN);
    CHECK(!app.running);

    app.running = true;
    key(&app, SDLK_ESCAPE);
    CHECK(!app.ui.menu_open);
    CHECK(app.running);

    key(&app, SDLK_RETURN);
    CHECK(app.running);

    printf("test_input: ok\n");
    return 0;
}
