#ifndef TSW_APP_H
#define TSW_APP_H

#include "assets.h"
#include "events.h"
#include "game.h"
#include "intro.h"
#include "log.h"
#include "map.h"
#include "render.h"
#include "state.h"
#include "options.h"
#include "ui.h"

#include <SDL3/SDL.h>

#include <stdbool.h>
#include <stddef.h>

#define APP_PATH_LEN 1024
#define APP_PATH_BUF (APP_PATH_LEN + 32)
#define FORECAST_LINES 16
#define FORECAST_LINE_LEN 160

#define APP_MAX_LANGUAGES 16

typedef struct App {
    Renderer render;
    Assets assets;
    Game game;
    MapBlock initial_map;
    bool have_initial_map;
    LogBuffer log;
    UiState ui;
    EventRuntime events;

    bool running;
    char forecast[FORECAST_LINES][FORECAST_LINE_LEN];
    int forecast_count;

    bool step_active;
    float step_from_x;
    float step_from_y;
    Uint64 step_start_ms;
    /* The arrow held for walking (0: none) and when its next step is due
     * (input_update). */
    SDL_Keycode walk_key;
    Uint64 next_walk_ms;
    Uint64 (*clock_fn)(void);
    LaunchIntro intro;
    /* Edit8's default text (data/text/en/ui.tsv index 0): the original's status
     * bar shows it during the launch intro. */
    char intro_text[256];

    char data_dir[APP_PATH_LEN];
    char save_dir[APP_PATH_LEN];
    char save_path[APP_PATH_LEN];
    char font_path[APP_PATH_LEN];
    /* Sound (NULL: silent). */
    struct Audio *audio;
    /* The Options menu (options.ini; written on every change unless
     * options_path is empty).  sound/music are the original's switches
     * "Wav Sound On" / "Background Music On" (state[0x59] = 0 /
     * state[0x5a] != 0). */
    Options options;
    char options_path[APP_PATH_LEN];
    /* "Floor N  Life M" per save slot 0..8 for the Save/Load lists ("" for
     * none), refreshed when they open. */
    char slot_info[UI_SLOT_COUNT + 1][48];
    /* The languages: data/text/<code>/ folders with a messages.tsv,
     * English first; the current one's shown name (interface.tsv
     * language.name). */
    char languages[APP_MAX_LANGUAGES][16];
    int language_count;
    char language_name[64];
} App;

bool app_init(App *app, const char *data_dir, const char *save_path,
              const char *save_dir, const char *font_path,
              bool no_intro, char *err, size_t errsz);
void app_run(App *app);
void app_shutdown(App *app);

Uint64 app_now(const App *app);
void app_start_intro(App *app);
void app_update_intro(App *app);
bool app_new_game(App *app, char *err, size_t errsz);
/* Put the sound switches into the state and start the floor's tune (or
 * stop it), as loadwork / syokidata2 do after a load or new game. */
void app_apply_sound_switches(App *app);
/* The Options menu.  Each change is applied at once (wavon1Click /
 * BGMON1Click / speedhigh.. / the Font menu) and written to options.ini. */
void app_set_sound(App *app, bool on);
void app_set_music(App *app, bool on);
void app_set_speed(App *app, int speed);
void app_set_text_size(App *app, OptTextSize text);
/* F11 / Alt+Enter: switch between the window and desktop fullscreen (the
 * canvas keeps its integer scaling) and remember it in options.ini. */
void app_toggle_fullscreen(App *app);
/* Fill app->languages from data_dir/text/. */
void app_scan_languages(App *app);
/* The first of `prefs` (language codes, most preferred first, like
 * "pl" or "pl_PL") that app->languages has, else English. */
const char *app_pick_language(const App *app, const char *const *prefs,
                              int count);
/* Switch the game text to data/text/<code>/ (one of app->languages) and
 * remember it in options.ini; false for an unknown code. */
bool app_set_language(App *app, const char *code);
/* The next language in app->languages, wrapping. */
void app_next_language(App *app);
/* Fill slot_info from the save directory. */
void app_refresh_slots(App *app);
/* Game > Ranking (HighScore1Click): the ranking window, area 10F first. */
void app_open_ranking(App *app);
/* Play what the event engine asked for since the last call. */
void app_pump_audio(App *app);
bool app_save_slot_path(const App *app, int slot, char *out, size_t outsz);
bool app_save_slot_exists(const App *app, int slot);
bool app_save_slot(App *app, int slot, char *err, size_t errsz);
/* Slot 1..8, or 0: the fairy's checkpoint save0.dat (zero checksums). */
bool app_load_slot(App *app, int slot, char *err, size_t errsz);
bool app_checkpoint_save(App *app, char *err, size_t errsz);
int64_t app_rank_score(App *app);
void app_maybe_trigger_floor_event(App *app);

#endif
