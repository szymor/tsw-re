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

static Uint64 fake_now_ms = 1000;

/* A player waits for a running timeline (pickup, door, battle animation)
 * before the next arrow key; windows that need an answer stay open. */
static void settle(App *app)
{
    EventEngine *e = &app->events.engine;
    for (int guard = 0; guard < 100000 && events_active(&app->events) &&
                        !e->dialog_open && !e->altar_open && !e->ranking_open &&
                        !e->death_prompt && !e->item_confirm && !e->fly_open &&
                        !e->wisdom_open;
         guard++) {
        fake_now_ms += 20;
        events_update(&app->events, fake_now_ms);
    }
}

static void key(App *app, SDL_Keycode code)
{
    if ((code == SDLK_UP || code == SDLK_DOWN || code == SDLK_LEFT ||
         code == SDLK_RIGHT) && app->ui.view == UI_VIEW_GAME)
        settle(app);
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

/* Run Timer2 in small steps until a dialogue opens or the engine idles. */
static void run_until_dialog(struct App *app)
{
    for (int guard = 0; guard < 4000 && events_active(&app->events) &&
                        !app->events.engine.dialog_open; guard++) {
        fake_now_ms += 20;
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
    options_defaults(&app.options);     /* options_path "": nothing written */
    app.options.music = false;          /* the timelines below without BGM */
    app.clock_fn = fake_clock;
    log_init(&app.log);
    ui_init(&app.ui);
    game_new(&app.game, &map);
    app.initial_map = map;
    app.have_initial_map = true;
    app.running = true;
    snprintf(app.save_dir, sizeof(app.save_dir), ".");
    place(&app, 1, 5, 5);
    /* Every move runs handan on the command VM. */
    char init_err[256] = "";
    CHECK(events_runtime_init(&app.events, &app,
                              TEST_SOURCE_DIR "/src/events/builders.lua",
                              event_log_cb, NULL,
                              init_err, sizeof(init_err)));

    /* The launch intro cannot be skipped from gameplay input. */
    app.intro.active = true;
    key(&app, SDLK_ESCAPE);
    key(&app, SDLK_RETURN);
    key(&app, SDLK_SPACE);
    CHECK(app.intro.active);
    app.intro.active = false;

    /* A held arrow walks at the NPCs' pace: a step at the press, then one
     * per speed interval (250 ms at Middle); the keyboard's repeats do
     * nothing; the release stops it. */
    {
        settle(&app);
        key(&app, SDLK_UP);
        CHECK(game_y(&app.game) == 4 && app.walk_key == SDLK_UP);
        key_event(&app, SDLK_UP, true);           /* an OS repeat */
        CHECK(game_y(&app.game) == 4);
        settle(&app);
        fake_now_ms = app.next_walk_ms - 1;
        input_update(&app);
        CHECK(game_y(&app.game) == 4);
        fake_now_ms = app.next_walk_ms;
        input_update(&app);
        CHECK(game_y(&app.game) == 3);
        settle(&app);
        fake_now_ms = app.next_walk_ms;
        input_update(&app);
        CHECK(game_y(&app.game) == 2);
        SDL_Event up;
        SDL_zero(up);
        up.type = SDL_EVENT_KEY_UP;
        up.key.key = SDLK_UP;
        input_handle_event(&app, &up);
        CHECK(app.walk_key == 0);
        settle(&app);
        fake_now_ms += 1000;
        input_update(&app);
        CHECK(game_y(&app.game) == 2);
        place(&app, 1, 5, 5);
        state_compute_checksums(&app.game.state);
    }

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
    CHECK(app.events.engine.item_confirm);       /* Yes/No panel */
    /* img8work.. focus Button37 ("Don't use"); Button38 ("Use") is under
     * it. */
    CHECK(app.events.engine.item_confirm_focus_no);  /* "Don't use" */
    key(&app, SDLK_LEFT);
    CHECK(!app.events.engine.item_confirm_focus_no);
    key(&app, SDLK_RETURN);                      /* Use (Button38) */
    settle(&app);
    CHECK(!app.events.engine.item_confirm);
    CHECK(app.game.map.bytes[map_floor_offset(1) + 10] == 0x06);
    CHECK(state_get(&app.game.state, ST_SPECIAL_FIRST + 12) == 0);

    /* A position-changing item clears any active slide: the Warp Staff
     * goes to the point mirror 0x78 - p (35 -> 85) when it is floor. */
    place(&app, 1, 2, 3);
    app.game.map.bytes[map_floor_offset(1) + 85] = 0x06;
    state_set(&app.game.state, ST_SPECIAL_FIRST + 7, 3);
    app.step_active = true;
    app.step_from_x = 4.0f;
    app.step_from_y = 5.0f;
    app.step_start_ms = fake_now_ms;
    ui_open_inventory(&app.ui);
    app.ui.inventory_selected = 7;
    key(&app, SDLK_RETURN);
    CHECK(app.ui.view == UI_VIEW_GAME);
    key(&app, SDLK_Y);
    CHECK(!app.step_active);
    CHECK(game_x(&app.game) == 85 % MAP_TILE_COLS);
    CHECK(game_y(&app.game) == 85 / MAP_TILE_COLS);
    CHECK(state_get(&app.game.state, ST_SPECIAL_FIRST + 7) == 2);
    settle(&app);

    /* Game-over overlay cannot be dismissed into dead gameplay; there are
     * no menu accelerators (F2 / F9 do nothing). */
    state_set(&app.game.state, ST_LIFE, 0);
    ui_open_game_over(&app.ui);
    key(&app, SDLK_F2);
    CHECK(app.ui.view == UI_VIEW_GAME_OVER);
    key(&app, SDLK_F10);
    CHECK(app.ui.view == UI_VIEW_GAME_OVER);
    key(&app, SDLK_F9);
    CHECK(app.running);
    snprintf(app.save_dir, sizeof(app.save_dir), "test-save-dir");
    char dead_err[128] = "";
    CHECK(!app_save_slot(&app, 1, dead_err, sizeof(dead_err)));
    CHECK(strstr(dead_err, "dead") != NULL);
    key(&app, SDLK_RETURN);
    CHECK(app.ui.view == UI_VIEW_GAME);
    CHECK(game_stat(&app.game, ST_LIFE) == 1000);

    /* Esc (or F10) opens the menu bar on the Game menu; New game asks,
     * opening on No. */
    place(&app, 3, 2, 2);
    state_set(&app.game.state, ST_LIFE, 1);
    key(&app, SDLK_ESCAPE);
    CHECK(app.ui.view == UI_VIEW_MENU && app.ui.menu_tab == UI_TAB_GAME);
    CHECK(app.ui.menu_selected == UI_GAME_NEW);
    key(&app, SDLK_RETURN);
    CHECK(app.ui.view == UI_VIEW_NEW_CONFIRM && !app.ui.confirm_yes);
    key(&app, SDLK_RETURN);                        /* No */
    CHECK(app.ui.view == UI_VIEW_MENU);
    CHECK(game_floor(&app.game) == 3);
    key(&app, SDLK_RETURN);
    key(&app, SDLK_LEFT);                          /* Yes */
    CHECK(app.ui.confirm_yes);
    key(&app, SDLK_RETURN);
    CHECK(app.ui.view == UI_VIEW_GAME);
    CHECK(game_floor(&app.game) == 1);
    CHECK(game_x(&app.game) == 5 && game_y(&app.game) == 10);
    CHECK(game_stat(&app.game, ST_LIFE) == 1000);

    key(&app, SDLK_F10);
    CHECK(app.ui.view == UI_VIEW_MENU);
    key_event(&app, SDLK_F10, true);
    CHECK(app.ui.view == UI_VIEW_MENU);
    key_event(&app, SDLK_F10, false);
    CHECK(app.ui.view == UI_VIEW_GAME);

    /* Game: New game, Load, Save, Ranking, Quit. */
    key(&app, SDLK_ESCAPE);
    key(&app, SDLK_DOWN);
    CHECK(app.ui.menu_selected == UI_GAME_LOAD);
    key(&app, SDLK_DOWN);
    CHECK(app.ui.menu_selected == UI_GAME_SAVE);
    key(&app, SDLK_DOWN);
    CHECK(app.ui.menu_selected == UI_GAME_RANKING);
    key(&app, SDLK_DOWN);
    CHECK(app.ui.menu_selected == UI_GAME_QUIT);
    key(&app, SDLK_DOWN);
    CHECK(app.ui.menu_selected == UI_GAME_NEW);    /* wraps */
    key(&app, SDLK_UP);
    key(&app, SDLK_RETURN);
    CHECK(!app.running);
    app.running = true;

    /* Options: Enter changes the value; the menus switch with Left /
     * Right and keep their place when a page closes. */
    key(&app, SDLK_RIGHT);
    CHECK(app.ui.menu_tab == UI_TAB_OPTIONS && app.ui.menu_selected == 0);
    key(&app, SDLK_RETURN);                        /* Sound effects off */
    CHECK(!app.options.sound && state_get(&app.game.state, 0x59) == 1);
    key(&app, SDLK_RETURN);
    CHECK(app.options.sound && state_get(&app.game.state, 0x59) == 0);
    key(&app, SDLK_DOWN);
    key(&app, SDLK_RETURN);                        /* on: the floor's tune */
    CHECK(app.options.music && state_get(&app.game.state, 0x5a) == 5);
    key(&app, SDLK_RETURN);                        /* Music off */
    CHECK(!app.options.music && state_get(&app.game.state, 0x5a) == 0);
    key(&app, SDLK_DOWN);
    CHECK(app.options.speed == EVM_SPEED_MIDDLE);
    key(&app, SDLK_RETURN);
    CHECK(app.options.speed == EVM_SPEED_LOW &&
          app.events.engine.vm.speed == EVM_SPEED_LOW);
    key(&app, SDLK_RETURN);
    CHECK(app.options.speed == EVM_SPEED_HIGH);
    key(&app, SDLK_RETURN);
    CHECK(app.options.speed == EVM_SPEED_MIDDLE);
    key(&app, SDLK_DOWN);
    key(&app, SDLK_RETURN);
    CHECK(app.options.text == OPT_TEXT_BIG);
    key(&app, SDLK_RETURN);
    CHECK(app.options.text == OPT_TEXT_TINY);
    app.options.text = OPT_TEXT_NORMAL;
    key(&app, SDLK_RIGHT);
    CHECK(app.ui.menu_tab == UI_TAB_HELP);
    key(&app, SDLK_RETURN);
    CHECK(app.ui.view == UI_VIEW_CONTROLS);
    key(&app, SDLK_ESCAPE);
    CHECK(app.ui.view == UI_VIEW_MENU && app.ui.menu_tab == UI_TAB_HELP);
    key(&app, SDLK_DOWN);
    key(&app, SDLK_RETURN);
    CHECK(app.ui.view == UI_VIEW_ABOUT);
    key(&app, SDLK_RETURN);
    CHECK(app.ui.view == UI_VIEW_MENU);
    key(&app, SDLK_RIGHT);
    CHECK(app.ui.menu_tab == UI_TAB_GAME);         /* wraps */
    key(&app, SDLK_ESCAPE);
    CHECK(app.ui.view == UI_VIEW_GAME);

    /* options.ini round trip; unknown keys and bad values are skipped. */
    {
        Options o, back;
        options_defaults(&o);
        o.sound = false;
        o.speed = EVM_SPEED_HIGH;
        o.text = OPT_TEXT_SMALL;
        CHECK(options_save(&o, "test-save-dir/opts/options.ini"));
        CHECK(options_load(&back, "test-save-dir/opts/options.ini"));
        CHECK(!back.sound && back.music && back.speed == EVM_SPEED_HIGH &&
              back.text == OPT_TEXT_SMALL);
        FILE *f = fopen("test-save-dir/opts/options.ini", "w");
        CHECK(f != NULL);
        if (f) {
            fputs("music = off\nspeed = warp\ncolour = red\n", f);
            fclose(f);
        }
        CHECK(options_load(&back, "test-save-dir/opts/options.ini"));
        CHECK(back.sound && !back.music && back.speed == EVM_SPEED_MIDDLE);
        CHECK(!options_load(&back, "test-save-dir/opts/missing.ini"));
        CHECK(back.music);                         /* the defaults */
        remove("test-save-dir/opts/options.ini");
        remove("test-save-dir/opts");
    }

    /* Save/load submenus and back-navigation. */
    key(&app, SDLK_ESCAPE);
    key(&app, SDLK_DOWN);
    key(&app, SDLK_DOWN);
    key(&app, SDLK_RETURN);
    CHECK(app.ui.view == UI_VIEW_SAVE);
    key(&app, SDLK_ESCAPE);
    CHECK(app.ui.view == UI_VIEW_MENU);
    key(&app, SDLK_UP);
    key(&app, SDLK_RETURN);
    CHECK(app.ui.view == UI_VIEW_LOAD);
    key(&app, SDLK_ESCAPE);
    CHECK(app.ui.view == UI_VIEW_MENU);
    key(&app, SDLK_ESCAPE);
    CHECK(app.ui.view == UI_VIEW_GAME);

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
    key(&app, SDLK_RETURN);                       /* opens on No */
    CHECK(app.ui.view == UI_VIEW_SAVE);
    key(&app, SDLK_RETURN);
    key(&app, SDLK_Y);
    CHECK(app.ui.view == UI_VIEW_GAME);

    /* The fairy's checkpoint (save0.dat) loads from the Load list's first
     * row; empty slots cannot be chosen. */
    place(&app, 6, 3, 4);
    CHECK(app_checkpoint_save(&app, err, sizeof(err)));
    place(&app, 1, 0, 0);
    key(&app, SDLK_ESCAPE);
    key(&app, SDLK_DOWN);                          /* Load */
    key(&app, SDLK_RETURN);
    CHECK(app.ui.view == UI_VIEW_LOAD && app.ui.load_checkpoint);
    CHECK(ui_load_rows(&app.ui) == 9);
    CHECK(ui_load_row_slot(&app.ui, 0) == 0);
    CHECK(strstr(app.slot_info[0], "Floor 6") != NULL);
    CHECK(app.slot_info[5][0] == '\0');
    app.ui.slot_selected = 5;                     /* save5: empty */
    key(&app, SDLK_RETURN);
    CHECK(app.ui.view == UI_VIEW_LOAD);
    app.ui.slot_selected = 0;
    key(&app, SDLK_RETURN);
    CHECK(app.ui.view == UI_VIEW_GAME);
    CHECK(game_floor(&app.game) == 6 && game_x(&app.game) == 3);
    remove("test-save-dir/save0.dat");

    remove("test-save-dir/save2.dat");
    remove("test-save-dir");

    char event_err[256] = "";
    snprintf(app.save_dir, sizeof(app.save_dir), "test-save-dir");

    /* Floor-10 event: boss, fairy, ranking log, save0 checkpoint. */
    game_new(&app.game, &app.initial_map);
    snprintf(app.save_dir, sizeof(app.save_dir), "test-save-dir");
    events_runtime_shutdown(&app.events);
    CHECK(events_runtime_init(&app.events, &app,
                              TEST_SOURCE_DIR "/src/events/builders.lua",
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
    app.step_active = false;
    key(&app, SDLK_UP);
    /* handan -> mevent -> zenolivework: the player steps onto the trigger,
     * the tile is cleared and the 41-command timeline starts. */
    CHECK(events_active(&app.events));
    CHECK(game_x(&app.game) == 4);
    CHECK(game_y(&app.game) == 8);
    /* idou's yusyaidou step is animated from the pre-trigger tile. */
    CHECK(app.step_active);
    CHECK(app.step_from_x == 4.0f && app.step_from_y == 9.0f);
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

    /* Floor-2 prisoner (handan raw 0x15 -> touzoku builder).  From (2,7):
     * sound, the cell door at index 0x43 opens (doorstackin), then the
     * zyouout window for sub-event 14.  Confirm (Button1Click case 0x0e):
     * he walks 68 -> 67 -> 66 -> 77.  From (0,6), bumping him at 77 gives
     * sub-event 15; case 0x0f walks him out through 88 and 99. */
    CHECK(map_tile(&app.game.map, 2, 6 * MAP_TILE_COLS + 2) == 0x15);
    key(&app, SDLK_UP);
    CHECK(app.events.engine.vm.count == 39);
    run_until_dialog(&app);
    CHECK(app.events.engine.dialog_open);
    CHECK(app.events.engine.dialog_id == 14);
    CHECK(dialog_has(&app, "What a relief"));
    CHECK(map_tile(&app.game.map, 2, 0x43) == 0x06);       /* door open */
    CHECK(game_x(&app.game) == 2);
    CHECK(game_y(&app.game) == 7);
    key(&app, SDLK_RETURN);
    run_until_dialog(&app);
    CHECK(!events_active(&app.events));
    CHECK(map_tile(&app.game.map, 2, 6 * MAP_TILE_COLS + 2) == 0x06);
    CHECK(map_tile(&app.game.map, 2, 7 * MAP_TILE_COLS + 0) == 0x15);
    state_set(&app.game.state, ST_X, 0);
    state_set(&app.game.state, ST_Y, 6);
    state_compute_checksums(&app.game.state);
    key(&app, SDLK_DOWN);
    run_until_dialog(&app);
    CHECK(app.events.engine.dialog_open);
    CHECK(app.events.engine.dialog_id == 15);
    CHECK(dialog_has(&app, "excuse my going first"));
    CHECK(game_x(&app.game) == 0);
    CHECK(game_y(&app.game) == 6);
    key(&app, SDLK_RETURN);
    run_until_dialog(&app);
    CHECK(!events_active(&app.events));
    CHECK(map_tile(&app.game.map, 2, 7 * MAP_TILE_COLS + 0) == 0x06);
    CHECK(map_tile(&app.game.map, 2, 8 * MAP_TILE_COLS + 0) == 0x06);
    CHECK(map_tile(&app.game.map, 2, 9 * MAP_TILE_COLS + 0) == 0x06);

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
    /* handan case 9: status text 0x89, the wall slides down (door kind 7),
     * the player stays put; the timeline runs without a dialogue. */
    key(&app, SDLK_RIGHT);
    CHECK(events_active(&app.events));
    CHECK(!app.events.engine.have_messages ||
          strstr(app.events.engine.status_text, "wall collapsed") != NULL);
    run_until_dialog(&app);
    CHECK(!events_active(&app.events));
    CHECK(!app.events.engine.dialog_open);
    CHECK(game_x(&app.game) == 7);
    CHECK(game_y(&app.game) == 10);
    CHECK(map_tile(&app.game.map, 5, 10 * MAP_TILE_COLS + 8) == 0x06);
    key(&app, SDLK_RIGHT);
    CHECK(game_x(&app.game) == 8);
    key(&app, SDLK_RIGHT);
    CHECK(game_x(&app.game) == 9);
    key(&app, SDLK_RIGHT);
    CHECK(game_x(&app.game) == 10);
    CHECK(state_get(&app.game.state, ST_SWORD_FLAG_FIRST) == 1);

    /* Merchant (handan raw 0x13 -> syounin): floor 6 sells a blue key for
     * 50 gold.  Yes/No question (Button2/Button7); Yes runs Button2Click,
     * then the result text waits for OK; the next talk gives the thank-you
     * text (talk flag 0x37), then zyouout ListBox2[0x1c + 0x3e]. */
    memset(app.initial_map.bytes + map_floor_offset(6), 0x06, MAP_TILES);
    app.initial_map.bytes[map_floor_offset(6) + 3 * MAP_TILE_COLS + 7] = 0x13;
    app.initial_map.bytes[map_floor_offset(7) + 1 * MAP_TILE_COLS + 5] = 0x13;
    game_new(&app.game, &app.initial_map);
    state_set(&app.game.state, ST_FLOOR, 6);
    state_set(&app.game.state, ST_FLOOR_LIMIT, 7);
    state_set(&app.game.state, ST_X, 7);
    state_set(&app.game.state, ST_Y, 4);
    state_set(&app.game.state, ST_GOLD, 100);
    state_compute_checksums(&app.game.state);
    int blue = (int)state_get(&app.game.state, ST_KEY_BLUE);
    key(&app, SDLK_UP);
    CHECK(app.events.engine.dialog_open && app.events.engine.dialog_choice);
    CHECK(app.events.engine.choice_no_enabled);
    CHECK(dialog_has(&app, "blue key"));
    /* The question opens on No (the no-action answer); Left is Yes,
     * Right No. */
    CHECK(app.events.engine.choice_focus_no);
    key(&app, SDLK_LEFT);
    CHECK(!app.events.engine.choice_focus_no);
    key(&app, SDLK_RIGHT);
    CHECK(app.events.engine.choice_focus_no);
    key(&app, SDLK_LEFT);
    key(&app, SDLK_RETURN);                    /* Yes */
    CHECK(state_get(&app.game.state, ST_GOLD) == 50);
    CHECK(state_get(&app.game.state, ST_KEY_BLUE) == blue + 1);
    CHECK(state_get(&app.game.state, 0x2b) == 1);
    CHECK(app.events.engine.dialog_open && !app.events.engine.dialog_choice);
    CHECK(dialog_has(&app, "Thank you very much"));
    key(&app, SDLK_RETURN);                    /* OK */
    run_until_dialog(&app);
    CHECK(!events_active(&app.events));
    key(&app, SDLK_UP);                        /* thank-you text */
    CHECK(app.events.engine.dialog_open && !app.events.engine.dialog_choice);
    CHECK(state_get(&app.game.state, 0x37) == 1);
    key(&app, SDLK_RETURN);
    run_until_dialog(&app);
    key(&app, SDLK_UP);                        /* nothing more to say */
    CHECK(app.events.engine.dialog_open);
    CHECK(app.events.engine.dialog_id == 0);
    key(&app, SDLK_RETURN);
    run_until_dialog(&app);
    CHECK(!events_active(&app.events));
    CHECK(game_x(&app.game) == 7 && game_y(&app.game) == 4);
    /* Floor 7: No (Escape) leaves everything as it was. */
    state_set(&app.game.state, ST_FLOOR, 7);
    state_set(&app.game.state, ST_X, 5);
    state_set(&app.game.state, ST_Y, 2);
    state_compute_checksums(&app.game.state);
    key(&app, SDLK_UP);
    CHECK(app.events.engine.dialog_choice);
    key(&app, SDLK_ESCAPE);
    CHECK(!events_active(&app.events));
    CHECK(state_get(&app.game.state, ST_GOLD) == 50);
    CHECK(state_get(&app.game.state, 0x2c) == 0);

    /* moncheck after a won battle (floor 8, 0x466814): with 52 and 54
     * floor and the door 42 still closed, the door opens. */
    memset(app.initial_map.bytes + map_floor_offset(8), 0x06, MAP_TILES);
    app.initial_map.bytes[map_floor_offset(8) + 54] = 0x3d;   /* slime */
    app.initial_map.bytes[map_floor_offset(8) + 42] = 0x04;
    game_new(&app.game, &app.initial_map);
    state_set(&app.game.state, ST_FLOOR, 8);
    state_set(&app.game.state, ST_FLOOR_LIMIT, 8);
    state_set(&app.game.state, ST_X, 9);
    state_set(&app.game.state, ST_Y, 4);
    state_compute_checksums(&app.game.state);
    key(&app, SDLK_RIGHT);
    /* The battle is a timeline (taisen): turns, gold, the cell cleared,
     * then the moncheck splice opens the door. */
    CHECK(events_active(&app.events));
    run_until_dialog(&app);
    CHECK(!events_active(&app.events));
    CHECK(map_tile(&app.game.map, 8, 54) == 0x06);
    CHECK(game_x(&app.game) == 10 && game_y(&app.game) == 4);
    CHECK(map_tile(&app.game.map, 8, 42) == 0x06);

    /* The altar (handan raw 0x10): Yes opens the menu (Button2Click mode 1);
     * Down moves from cancel to life; Enter buys: 20 gold, life +100. */
    memset(app.initial_map.bytes + map_floor_offset(4), 0x06, MAP_TILES);
    app.initial_map.bytes[map_floor_offset(4) + 5] = 0x10;
    game_new(&app.game, &app.initial_map);
    state_set(&app.game.state, ST_FLOOR, 4);
    state_set(&app.game.state, ST_FLOOR_LIMIT, 4);
    state_set(&app.game.state, ST_X, 5);
    state_set(&app.game.state, ST_Y, 1);
    state_set(&app.game.state, ST_GOLD, 100);
    state_compute_checksums(&app.game.state);
    int life0 = (int)state_get(&app.game.state, ST_LIFE);
    key(&app, SDLK_UP);
    CHECK(game_y(&app.game) == 1);                 /* stays in front */
    CHECK(app.events.engine.dialog_choice);
    CHECK(dialog_has(&app, " 20 "));
    key(&app, SDLK_Y);                             /* Yes, directly */
    CHECK(app.events.engine.altar_open);
    CHECK(app.events.engine.altar_focus == 3);
    key(&app, SDLK_1);                             /* the first offer */
    CHECK(!app.events.engine.altar_open);
    CHECK(state_get(&app.game.state, ST_GOLD) == 80);
    CHECK(state_get(&app.game.state, ST_LIFE) == life0 + 100);
    CHECK(state_get(&app.game.state, 0x0b) == 1);
    run_until_dialog(&app);
    CHECK(!events_active(&app.events));
    key(&app, SDLK_UP);                            /* next offer: 40 */
    CHECK(dialog_has(&app, " 40 "));
    key(&app, SDLK_N);
    CHECK(!events_active(&app.events));

    char save_err[256] = "";

    /* Floor 10: stepping onto the trigger (5,4) runs mevent -> ichicheck:
     * the 462-command boss-room timeline; sub-event 23 opens first.
     * Loading a floor-10 save starts nothing by itself. */
    memset(app.initial_map.bytes + map_floor_offset(10), 0x06, MAP_TILES);
    app.initial_map.bytes[map_floor_offset(10) + 4 * MAP_TILE_COLS + 5] = 0x07;
    game_new(&app.game, &app.initial_map);
    state_set(&app.game.state, ST_FLOOR, 10);
    state_set(&app.game.state, ST_FLOOR_LIMIT, 10);
    state_set(&app.game.state, ST_X, 5);
    state_set(&app.game.state, ST_Y, 5);
    state_compute_checksums(&app.game.state);
    CHECK(app_save_slot(&app, 2, save_err, sizeof(save_err)));
    game_new(&app.game, &app.initial_map);
    CHECK(app_load_slot(&app, 2, save_err, sizeof(save_err)));
    CHECK(!events_active(&app.events));
    remove("test-save-dir/save2.dat");
    key(&app, SDLK_UP);
    CHECK(game_x(&app.game) == 5 && game_y(&app.game) == 4);
    CHECK(map_tile(&app.game.map, 10, 4 * MAP_TILE_COLS + 5) == 0x06);
    CHECK(app.events.engine.vm.count == 462);
    run_until_dialog(&app);
    CHECK(app.events.engine.dialog_open && app.events.engine.dialog_id == 23);
    events_cancel(&app.events);

    /* The fairy (raw 0x17 -> yousei): score/rank/area, the save0.dat
     * checkpoint, sub-event 26; its confirm sets the area flag 0x6a and,
     * on difficulty 0, shows the ranking window (Timer2 stays off until
     * the window closes: form2onclose). */
    app.initial_map.bytes[map_floor_offset(10) + 3 * MAP_TILE_COLS + 5] = 0x17;
    game_new(&app.game, &app.initial_map);
    state_set(&app.game.state, ST_FLOOR, 10);
    state_set(&app.game.state, ST_FLOOR_LIMIT, 10);
    state_set(&app.game.state, ST_X, 5);
    state_set(&app.game.state, ST_Y, 4);
    state_set(&app.game.state, ST_LIFE, 1500);
    state_set(&app.game.state, ST_ATTACK, 30);
    state_set(&app.game.state, ST_DEFENSE, 30);
    state_compute_checksums(&app.game.state);
    /* No ranking files yet: the temporary ranking (rank 4). */
    remove("test-save-dir/option1.dat");
    remove("test-save-dir/option11.dat");
    snprintf(app.events.engine.ranking_dir,
             sizeof(app.events.engine.ranking_dir), "%s", app.save_dir);
    key(&app, SDLK_UP);
    CHECK(state_get(&app.game.state, 0x55) == 75);   /* score */
    CHECK(state_get(&app.game.state, 0x57) == 1);    /* area 1 */
    CHECK(state_get(&app.game.state, 0x0e) == 1);
    SaveData checkpoint;
    char checkpoint_err[256] = "";
    CHECK(save_load_file("test-save-dir/save0.dat", &checkpoint,
                         checkpoint_err, sizeof(checkpoint_err)));
    CHECK(checkpoint.status == STATE_OK_CHECKPOINT);
    CHECK(state_get(&checkpoint.state, 0x55) == 75);
    CHECK(state_get(&checkpoint.state, 0x56) == 4);  /* temporary ranking */
    CHECK(state_get(&checkpoint.state, ST_FLOOR) == 10);
    run_until_dialog(&app);
    CHECK(app.events.engine.dialog_open && app.events.engine.dialog_id == 26);
    CHECK(dialog_has(&app, " 75 "));
    key(&app, SDLK_RETURN);
    CHECK(state_get(&app.game.state, ST_EVENT_FLAG_FLOOR10) == 1);
    CHECK(app.events.engine.ranking_open);
    CHECK(app.events.engine.ranking_name_entry);   /* ranked: InputQuery */
    CHECK(events_active(&app.events));
    SDL_Event text;
    SDL_zero(text);
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.text = "Hero";
    input_handle_event(&app, &text);
    key(&app, SDLK_RETURN);                        /* OK */
    CHECK(!app.events.engine.ranking_name_entry);
    CHECK(strcmp(app.events.engine.ranking.names[3], "Hero") == 0);
    /* The area buttons as tabs: Right shows the next area's table, Left
     * comes back to the fairy's (whose table is untouched). */
    int own = app.events.engine.ranking_area;
    CHECK(app.events.engine.ranking_view_area == own);
    CHECK(event_engine_ranking_shown(&app.events.engine) ==
          &app.events.engine.ranking);
    key(&app, SDLK_RIGHT);
    CHECK(app.events.engine.ranking_view_area == own % 5 + 1);
    CHECK(event_engine_ranking_shown(&app.events.engine) ==
          &app.events.engine.ranking_view);
    key(&app, SDLK_LEFT);
    CHECK(app.events.engine.ranking_view_area == own);
    CHECK(strcmp(app.events.engine.ranking.names[3], "Hero") == 0);
    key(&app, SDLK_RETURN);                        /* close: Timer2 on */
    CHECK(!app.events.engine.ranking_open);
    CHECK(app.events.engine.timer2);
    CHECK(events_active(&app.events));
    run_until_dialog(&app);
    CHECK(!events_active(&app.events));
    CHECK(app.events.engine.vm.count == 0);
    CHECK(map_tile(&app.game.map, 10, 0x1b) == 0x06);

    /* The Orb of Wisdom (img5work): Button11..34 enabled by the old men's
     * flags (here the first, 0x44, and the third, 0x37).  The remake lists
     * the men heard: the first one's words show at once (its
     * Button11Click: state 0xa7 = 1, no Button1), Down shows the next,
     * Enter closes (Button35). */
    for (int f = 0x37; f <= 0x53; f++)
        state_set(&app.game.state, f, 0);
    state_set(&app.game.state, 0x44, 2);
    state_set(&app.game.state, 0x37, 2);
    state_set(&app.game.state, ST_SPECIAL_FIRST + 1, 1);
    state_compute_checksums(&app.game.state);
    key(&app, SDLK_RETURN);
    CHECK(app.ui.view == UI_VIEW_INVENTORY);
    app.ui.inventory_selected = 1;
    key(&app, SDLK_RETURN);
    EventEngine *we = &app.events.engine;
    CHECK(we->wisdom_open);
    CHECK(we->wisdom_enabled[0] && !we->wisdom_enabled[1] &&
          we->wisdom_enabled[2] && !we->wisdom_enabled[23]);
    CHECK(we->wisdom_focus == 0);
    CHECK(events_active(&app.events));
    CHECK(event_engine_dialog(we) != NULL);        /* man 1's words */
    CHECK(!we->dialog_open);                       /* no Button1 */
    CHECK(state_get(&app.game.state, 0xa7) == 1);
    CHECK((we->aux.v[EVAUX_MESSAGE_SELECTOR] & 0xffff) == 0);
    key(&app, SDLK_DOWN);                          /* skips the unheard */
    CHECK(we->wisdom_focus == 2);
    CHECK((we->aux.v[EVAUX_MESSAGE_SELECTOR] & 0xffff) == 2);
    key(&app, SDLK_DOWN);                          /* wraps */
    CHECK(we->wisdom_focus == 0);
    key(&app, SDLK_UP);
    CHECK(we->wisdom_focus == 2);
    key(&app, SDLK_RETURN);                        /* close (Button35) */
    CHECK(!we->wisdom_open);
    CHECK(event_engine_dialog(we) == NULL);
    CHECK(state_get(&app.game.state, 0xa7) == 0);
    CHECK(!events_active(&app.events));
    remove("test-save-dir/save0.dat");
    remove("test-save-dir");
    events_runtime_shutdown(&app.events);

    printf("test_input: ok\n");
    return 0;
}
