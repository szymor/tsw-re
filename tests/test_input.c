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

static void event_log_cb(void *user, const char *message)
{
    (void)user;
    (void)message;
}

static Uint64 fake_now_ms = 1000;

struct App;
static bool dialog_has(struct App *app, const char *needle);
static void run_timeline(struct App *app);


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

/* Text check that tolerates a missing data/text/messages.tsv (the list is
 * extracted from the user's own TSW.exe and may be absent in CI). */
static bool dialog_has(struct App *app, const char *needle)
{
    const char *text = event_engine_dialog(&app->events.engine);
    if (!text)
        return false;
    if (!app->events.engine.have_messages)
        return true;
    return strstr(text, needle) != NULL;
}

/* Let Timer2 consume the queued commands. */
static void run_timeline(struct App *app)
{
    for (int guard = 0; guard < 64 && app->events.engine.vm.count > 0;
         guard++) {
        fake_now_ms += 250;
        events_update(&app->events, fake_now_ms);
    }
}

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

    /* Lua event runtime: message -> choice -> state mutation. */
    FILE *script = fopen("test_events.lua", "w");
    CHECK(script != NULL);
    fputs("events = {}\n"
          "events['1:0:1'] = function()\n"
          "  message('hello')\n"
          "  local c = choice('pick', 'A', 'B')\n"
          "  set_state(0, c)\n"
          "end\n"
          "events['1:0:2'] = function()\n"
          "  wait(2)\n"
          "  set_state(0, 777)\n"
          "end\n", script);
    fclose(script);
    char event_err[256] = "";
    CHECK(events_runtime_init(&app.events, &app, "test_events.lua",
                              event_log_cb, NULL,
                              event_err, sizeof(event_err)));
    snprintf(app.save_dir, sizeof(app.save_dir), "test-save-dir");
    CHECK(events_trigger(&app.events, 1, 0, 1));
    CHECK(app.events.active);
    CHECK(app.events.wait == EVENT_WAIT_MESSAGE);
    CHECK(!app_save_slot(&app, 1, event_err, sizeof(event_err)));
    CHECK(strstr(event_err, "event") != NULL);
    CHECK(events_handle_key(&app.events, SDLK_RETURN, false));
    CHECK(app.events.wait == EVENT_WAIT_CHOICE);
    CHECK(events_handle_key(&app.events, SDLK_DOWN, false));
    CHECK(app.events.choice_selected == 1);
    CHECK(events_handle_key(&app.events, SDLK_RETURN, false));
    CHECK(!app.events.active);
    CHECK(state_get(&app.game.state, ST_LIFE) == 2);

    fake_now_ms = 1000;
    CHECK(events_trigger(&app.events, 1, 0, 2));
    CHECK(app.events.wait == EVENT_WAIT_TICKS);
    events_update(&app.events, fake_now_ms + 10);
    CHECK(app.events.active);
    events_update(&app.events, fake_now_ms + 100);
    CHECK(!app.events.active);
    CHECK(state_get(&app.game.state, ST_LIFE) == 777);

    events_runtime_shutdown(&app.events);
    remove("test_events.lua");

    /* Floor-10 event: boss, fairy, ranking log, save0 checkpoint. */
    game_new(&app.game, &app.initial_map);
    snprintf(app.save_dir, sizeof(app.save_dir), "test-save-dir");
    CHECK(events_runtime_init(&app.events, &app,
                              TEST_SOURCE_DIR "/src/events/events.lua",
                              event_log_cb, NULL,
                              event_err, sizeof(event_err)));

    /* Floor-3 Zeno trigger at (4,8): actors, dialogues, cutscene, teleport. */
    memset(app.initial_map.bytes + map_floor_offset(3), 0x06, MAP_TILES);
    app.initial_map.bytes[map_floor_offset(3) + 8 * MAP_TILE_COLS + 4] = 0x07;
    /* The prisoner marker is part of the original initial map (floor 2,
     * (2,6)); the capture does not place it. */
    app.initial_map.bytes[map_floor_offset(2) + 6 * MAP_TILE_COLS + 2] = 0x15;
    game_new(&app.game, &app.initial_map);
    state_set(&app.game.state, ST_FLOOR, 3);
    state_set(&app.game.state, ST_FLOOR_LIMIT, 3);
    state_set(&app.game.state, ST_X, 4);
    state_set(&app.game.state, ST_Y, 9);
    state_compute_checksums(&app.game.state);
    CHECK(map_tile(&app.game.map, 3, 8 * MAP_TILE_COLS + 4) == 0x07);
    key(&app, SDLK_UP);
    /* handan -> mevent -> zenolivework: the player steps onto the trigger,
     * the tile is cleared and the 41-command timeline starts. */
    CHECK(events_active(&app.events));
    CHECK(!app.events.active);
    CHECK(game_x(&app.game) == 4);
    CHECK(game_y(&app.game) == 8);
    CHECK(map_tile(&app.game.map, 3, 8 * MAP_TILE_COLS + 4) == 0x06);
    CHECK(app.events.engine.vm.count == 41);
    CHECK(!app_save_slot(&app, 1, event_err, sizeof(event_err)));
    for (int guard = 0; guard < 64 && !app.events.engine.dialog_open;
         guard++) {
        fake_now_ms += 250;
        events_update(&app.events, fake_now_ms);
    }
    /* Sub-event 20 (op 0x0a): Timer2 stops and the zyouout window shows
     * ListBox2[9] (or a placeholder without data/text/messages.tsv). */
    CHECK(app.events.engine.dialog_open);
    CHECK(app.events.engine.dialog_id == 20);
    CHECK(app.events.engine.vm.count == 0);
    CHECK(!app.events.active);
    CHECK(events_active(&app.events));
    CHECK(event_engine_dialog(&app.events.engine) != NULL);
    CHECK(dialog_has(&app, "Magic Sergeant, Zeno"));
    /* Confirm: Button1Click case 20 runs openning2 (651 commands).  Play
     * the rest of the opening, confirming each dialogue as it appears:
     * 21 ("What? Oh!"), then the wake-up 11, 11, 12, 13 after openning31's
     * capture. */
    key(&app, SDLK_RETURN);
    CHECK(state_get(&app.game.state, 0x64) == 1);
    CHECK(app.events.engine.vm.count == 651);
    CHECK(events_active(&app.events));
    CHECK(!app_save_slot(&app, 1, event_err, sizeof(event_err)));
    int seen[8], nseen = 0;
    for (int guard = 0; guard < 4000 && events_active(&app.events); guard++) {
        fake_now_ms += 400;
        events_update(&app.events, fake_now_ms);
        if (app.events.engine.dialog_open) {
            if (nseen < 8)
                seen[nseen++] = app.events.engine.dialog_id;
            if (app.events.engine.dialog_id == 21)
                CHECK(dialog_has(&app, "What? Oh!"));
            key(&app, SDLK_RETURN);
        }
        CHECK(!app.events.active);         /* no transitional coroutine */
    }
    CHECK(nseen == 5);
    if (nseen == 5)
        CHECK(seen[0] == 21 && seen[1] == 11 && seen[2] == 11 &&
              seen[3] == 12 && seen[4] == 13);
    CHECK(!events_active(&app.events));
    CHECK(game_floor(&app.game) == 2);
    CHECK(game_x(&app.game) == 2);
    CHECK(game_y(&app.game) == 7);
    CHECK(state_get(&app.game.state, ST_FLOOR_LIMIT) == 2);
    CHECK(state_get(&app.game.state, ST_LIFE) == 400);
    CHECK(state_get(&app.game.state, ST_ATTACK) == 10);
    CHECK(state_get(&app.game.state, ST_DEFENSE) == 10);
    CHECK(state_get(&app.game.state, 0x65) == 1);
    CHECK(state_get(&app.game.state, 0x5d) == 1);
    CHECK(map_tile(&app.game.map, 3, 8 * MAP_TILE_COLS + 4) == 0x06);
    /* No placeholder flag: the one-shot is the cleared tile. */
    CHECK(state_get(&app.game.state, 0x9b) == 0);
    CHECK(app.events.engine.cutscene.image == 0);
    CHECK(app_save_slot(&app, 1, event_err, sizeof(event_err)));

    /* Floor-2 prisoner: bump first dialogue, marker moves, second dialogue. */
    CHECK(map_tile(&app.game.map, 2, 6 * MAP_TILE_COLS + 2) == 0x15);
    key(&app, SDLK_UP);
    CHECK(app.events.active);
    CHECK(strstr(app.events.message, "What a relief") != NULL);
    CHECK(game_x(&app.game) == 2);
    CHECK(game_y(&app.game) == 7);
    key(&app, SDLK_RETURN);
    CHECK(!app.events.active);
    CHECK(map_tile(&app.game.map, 2, 6 * MAP_TILE_COLS + 2) == 0x06);
    CHECK(map_tile(&app.game.map, 2, 7 * MAP_TILE_COLS + 0) == 0x15);
    state_set(&app.game.state, ST_X, 0);
    state_set(&app.game.state, ST_Y, 8);
    state_compute_checksums(&app.game.state);
    key(&app, SDLK_UP);
    CHECK(app.events.active);
    CHECK(strstr(app.events.message, "excuse my going first") != NULL);
    CHECK(game_x(&app.game) == 0);
    CHECK(game_y(&app.game) == 8);
    key(&app, SDLK_RETURN);
    CHECK(!app.events.active);
    CHECK(map_tile(&app.game.map, 2, 7 * MAP_TILE_COLS + 0) == 0x06);

    /* Old men (handan raw 0x19 -> roujin builder): the zyouout window shows
     * ListBox2[selector + 0x3e]; the boost/Orb commands run on Timer2 while
     * it is open; Enter confirms (Button1Click case 0). */
    memset(app.initial_map.bytes + map_floor_offset(2), 0x06, MAP_TILES);
    app.initial_map.bytes[map_floor_offset(2) + 6 * MAP_TILE_COLS + 10] = 0x19;
    game_new(&app.game, &app.initial_map);
    state_set(&app.game.state, ST_FLOOR, 2);
    state_set(&app.game.state, ST_FLOOR_LIMIT, 2);
    state_set(&app.game.state, ST_X, 9);
    state_set(&app.game.state, ST_Y, 6);
    state_compute_checksums(&app.game.state);
    key(&app, SDLK_RIGHT);
    CHECK(!app.events.active);
    CHECK(app.events.engine.dialog_open);
    CHECK(dialog_has(&app, "raise your offensive"));
    CHECK(game_x(&app.game) == 9);
    CHECK(state_get(&app.game.state, ST_EVENT_FLAG_OLD_MAN_F2) == 1);
    CHECK(state_get(&app.game.state, 0x42) == 1);
    CHECK(state_get(&app.game.state, ST_ATTACK) == 103);
    CHECK(state_get(&app.game.state, ST_DEFENSE) == 103);
    run_timeline(&app);
    CHECK(app.events.engine.vm.count == 0);
    CHECK(events_active(&app.events));        /* window still waits */
    key(&app, SDLK_RETURN);
    CHECK(!events_active(&app.events));
    key(&app, SDLK_RIGHT);
    CHECK(app.events.engine.dialog_open);
    CHECK(dialog_has(&app, "Good luck"));
    key(&app, SDLK_RETURN);
    CHECK(!events_active(&app.events));

    memset(app.initial_map.bytes + map_floor_offset(3), 0x06, MAP_TILES);
    app.initial_map.bytes[map_floor_offset(3) + 3 * MAP_TILE_COLS + 10] = 0x19;
    game_new(&app.game, &app.initial_map);
    state_set(&app.game.state, ST_FLOOR, 3);
    state_set(&app.game.state, ST_FLOOR_LIMIT, 3);
    state_set(&app.game.state, ST_X, 9);
    state_set(&app.game.state, ST_Y, 3);
    state_compute_checksums(&app.game.state);
    key(&app, SDLK_RIGHT);
    CHECK(app.events.engine.dialog_open);
    CHECK(dialog_has(&app, "orb of the hero"));
    CHECK(state_get(&app.game.state, ST_EVENT_FLAG_OLD_MAN_F3) == 1);
    CHECK(state_get(&app.game.state, ST_SPECIAL_FIRST) == 2);
    run_timeline(&app);
    key(&app, SDLK_RETURN);
    CHECK(!events_active(&app.events));
    /* Button1Click case 0: the Orb of the Hero becomes usable (2 -> 1). */
    CHECK(state_get(&app.game.state, ST_SPECIAL_FIRST) == 1);

    /* Floor-5 secret wall at (8,10) must open the pocket with the Iron Sword. */
    memset(app.initial_map.bytes + map_floor_offset(5), 0x06, MAP_TILES);
    app.initial_map.bytes[map_floor_offset(5) + 10 * MAP_TILE_COLS + 8] = 0x09;
    app.initial_map.bytes[map_floor_offset(5) + 10 * MAP_TILE_COLS + 10] = 0x33;
    game_new(&app.game, &app.initial_map);
    state_set(&app.game.state, ST_FLOOR, 5);
    state_set(&app.game.state, ST_FLOOR_LIMIT, 5);
    state_set(&app.game.state, ST_X, 7);
    state_set(&app.game.state, ST_Y, 10);
    state_compute_checksums(&app.game.state);
    key(&app, SDLK_RIGHT);
    CHECK(app.events.active);
    CHECK(strstr(app.events.message, "wall collapsed") != NULL);
    key(&app, SDLK_RETURN);
    CHECK(!app.events.active);
    CHECK(game_x(&app.game) == 8);
    CHECK(game_y(&app.game) == 10);
    CHECK(map_tile(&app.game.map, 5, 10 * MAP_TILE_COLS + 8) == 0x06);
    key(&app, SDLK_RIGHT);
    CHECK(game_x(&app.game) == 9);
    key(&app, SDLK_RIGHT);
    CHECK(game_x(&app.game) == 10);
    CHECK(state_get(&app.game.state, ST_SWORD_FLAG_FIRST) == 1);

    char save_err[256] = "";

    /* Loading an unfinished floor-10 save must activate the boss event. */
    game_new(&app.game, &app.initial_map);
    state_set(&app.game.state, ST_FLOOR, 10);
    state_set(&app.game.state, ST_FLOOR_LIMIT, 10);
    state_compute_checksums(&app.game.state);
    CHECK(app_save_slot(&app, 2, save_err, sizeof(save_err)));
    game_new(&app.game, &app.initial_map);
    CHECK(app_load_slot(&app, 2, save_err, sizeof(save_err)));
    CHECK(app.events.active);
    CHECK(game_floor(&app.game) == 10);
    events_cancel(&app.events);
    remove("test-save-dir/save2.dat");
    remove("test-save-dir");

    game_new(&app.game, &app.initial_map);
    state_set(&app.game.state, ST_FLOOR, 10);
    state_set(&app.game.state, ST_FLOOR_LIMIT, 10);
    state_compute_checksums(&app.game.state);
    app_maybe_trigger_floor_event(&app);
    CHECK(app.events.active);
    for (int guard = 0; guard < 32 && app.events.active; guard++) {
        if (app.events.wait == EVENT_WAIT_MESSAGE)
            events_handle_key(&app.events, SDLK_RETURN, false);
        else if (app.events.wait == EVENT_WAIT_CHOICE)
            events_handle_key(&app.events, SDLK_RETURN, false);
        else
            break;
    }
    CHECK(!app.events.active);
    CHECK(state_get(&app.game.state, ST_EVENT_FLAG_FLOOR10) == 1);
    SaveData checkpoint;
    char checkpoint_err[256] = "";
    CHECK(save_load_file("test-save-dir/save0.dat", &checkpoint,
                         checkpoint_err, sizeof(checkpoint_err)));
    CHECK(checkpoint.status == STATE_OK_CHECKPOINT);
    CHECK(state_get(&checkpoint.state, ST_EVENT_FLAG_FLOOR10) == 1);
    CHECK(state_get(&checkpoint.state, ST_FLOOR) == 10);
    app_maybe_trigger_floor_event(&app);
    CHECK(!app.events.active);
    remove("test-save-dir/save0.dat");
    remove("test-save-dir");
    events_runtime_shutdown(&app.events);

    printf("test_input: ok\n");
    return 0;
}
