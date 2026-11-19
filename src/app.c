#include "app.h"

#include "itext.h"
#include "audio.h"
#include "items.h"

#include "events.h"
#include "fileio.h"
#include "input.h"
#include "messages.h"
#include "pictures.h"
#include "save.h"
#include "version.h"

#include <SDL3_image/SDL_image.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Uint64 app_now(const App *app)
{
    if (app && app->clock_fn)
        return app->clock_fn();
    return SDL_GetTicks();
}

void app_start_intro(App *app)
{
    if (!app) return;
    intro_start(&app->intro, app_now(app));
    /* The form's Edit8 (the status bar) still holds its DFM default text:
     * nothing writes the bar until gameover -> syokidata2 -> disp at the end
     * of timer4ontimer. */
    app->intro_text[0] = '\0';
    char path[APP_PATH_BUF];
    messages_path(path, sizeof(path), app->data_dir, "ui.tsv");
    MessageTable ui;
    if (messages_load(&ui, path, NULL, 0)) {
        const char *text = messages_get(&ui, 0);
        if (text)
            snprintf(app->intro_text, sizeof(app->intro_text), "%s", text);
        messages_free(&ui);
    }
    if (app->intro_text[0])
        log_add(&app->log, "%s", app->intro_text);
}

void app_update_intro(App *app)
{
    if (!app || !app->intro.active) return;
    intro_update(&app->intro, app_now(app));
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
    size_t n = 0;
    char *data = file_load(path, &n);
    if (!data) {
        if (err && errsz) snprintf(err, errsz, "cannot open %s", path);
        return false;
    }
    bool ok = map_parse((const uint8_t *)data, n, out, err, errsz);
    file_free(data);
    return ok;
}

static bool find_builders_script(const char *data_dir, char *out, size_t outsz)
{
    char path[APP_PATH_BUF];
    snprintf(path, sizeof(path), "%s/events/builders.lua", data_dir);
    if (events_file_exists(path)) {
        snprintf(out, outsz, "%s", path);
        return true;
    }
    /* The repository: data/ next to src/events/. */
    snprintf(path, sizeof(path), "%s/../src/events/builders.lua", data_dir);
    if (events_file_exists(path)) {
        snprintf(out, outsz, "%s", path);
        return true;
    }
    snprintf(path, sizeof(path), "src/events/builders.lua");
    if (events_file_exists(path)) {
        snprintf(out, outsz, "%s", path);
        return true;
    }
    return false;
}

static void cleanup_partial(App *app)
{
    audio_shutdown(app->audio);
    app->audio = NULL;
    events_runtime_shutdown(&app->events);
    assets_shutdown(&app->assets);
    render_shutdown(&app->render);
    TTF_Quit();
    SDL_Quit();
}

bool app_save_slot_path(const App *app, int slot, char *out, size_t outsz)
{
    if (!app || !out || slot < 0 || slot > UI_SLOT_COUNT)
        return false;       /* 0: save0.dat, the fairy's checkpoint */
    return snprintf(out, outsz, "%s/save%d.dat", app->save_dir, slot) < (int)outsz;
}

bool app_save_slot_exists(const App *app, int slot)
{
    char path[APP_PATH_BUF];
    if (!app_save_slot_path(app, slot, path, sizeof(path)))
        return false;
    return path_is_file(path);
}

void app_apply_sound_switches(App *app)
{
    if (!app)
        return;
    StateBlock *s = &app->game.state;
    app->events.engine.bgm_switch = app->options.music;
    s->raw[0x59] = app->options.sound ? 0 : 1;
    if (!app->options.music)
        s->raw[0x5a] = 0;
    else if (s->raw[0x5a] == 0)
        s->raw[0x5a] = 5;               /* any id: soundcheck picks */
    event_engine_floor_bgm(&app->events.engine);
    state_compute_checksums(s);
}

static void save_options(App *app)
{
    if (app->options_path[0] && !options_save(&app->options, app->options_path))
        log_add(&app->log, tx(ITX_LOG_OPTIONS_FAILED), app->options_path);
}

void app_toggle_fullscreen(App *app)
{
    if (!app) return;
    bool now = app->options.fullscreen;
    if (app->render.window)             /* the desktop may have changed it */
        now = (SDL_GetWindowFlags(app->render.window) & SDL_WINDOW_FULLSCREEN) != 0;
    app->options.fullscreen = !now;
    if (app->render.window &&
        !SDL_SetWindowFullscreen(app->render.window, app->options.fullscreen))
        log_add(&app->log, "Fullscreen failed: %s", SDL_GetError());
    save_options(app);
}

void app_set_sound(App *app, bool on)
{
    if (!app) return;
    app->options.sound = on;
    /* wavon1Click: state[0x59] 0 = on. */
    app->game.state.raw[0x59] = on ? 0 : 1;
    state_compute_checksums(&app->game.state);
    save_options(app);
}

void app_set_music(App *app, bool on)
{
    if (!app) return;
    app->options.music = on;
    app->events.engine.bgm_switch = on;
    /* BGMON1Click: off stops the tune and clears state[0x5a]; on runs
     * soundcheck + soundplay (unless Timer3, the intro, is running). */
    StateBlock *s = &app->game.state;
    if (!on) {
        s->raw[0x5a] = 0;
        event_engine_floor_bgm(&app->events.engine);    /* a stop */
    } else {
        s->raw[0x5a] = 5;
        if (!app->intro.active)
            event_engine_floor_bgm(&app->events.engine);
    }
    state_compute_checksums(s);
    save_options(app);
}

void app_set_speed(App *app, int speed)
{
    if (!app) return;
    app->options.speed = speed;
    EventVm *vm = &app->events.engine.vm;
    vm->speed = speed;
    /* speedhigh / speedmiddle / speedlow set Timer2's interval. */
    vm->interval_ms = event_vm_speed_interval_ms(speed);
    save_options(app);
}

void app_set_text_size(App *app, OptTextSize text)
{
    if (!app) return;
    app->options.text = text;
    save_options(app);
}

/* data/text/<code>/ folders that hold a messages.tsv, English first,
 * then by code. */
void app_scan_languages(App *app)
{
    app->language_count = 0;
    char dir[APP_PATH_BUF];
    snprintf(dir, sizeof(dir), "%s/text", app->data_dir);
    int n = 0;
    char **names = SDL_GlobDirectory(dir, "*", 0, &n);
    for (int i = 0; names && i < n; i++) {
        const char *code = names[i];
        char probe[APP_PATH_BUF + 64];
        snprintf(probe, sizeof(probe), "%s/%s/messages.tsv", dir, code);
        if (strchr(code, '/') || strchr(code, '\\') ||
            strlen(code) >= sizeof(app->languages[0]) || !path_is_file(probe))
            continue;
        if (app->assets.ui[UI_FONT_14]) {
            /* e.g. Japanese without a Japanese font on the system */
            char name[64];
            messages_lang_name(app->data_dir, code, name, sizeof(name));
            if (!assets_can_draw(&app->assets, name))
                continue;
        }
        if (app->language_count >= APP_MAX_LANGUAGES)
            break;
        /* insertion sort: "en" first, then by code */
        int at = app->language_count;
        while (at > 0) {
            const char *prev = app->languages[at - 1];
            bool before = strcmp(code, "en") == 0 ||
                          (strcmp(prev, "en") != 0 && strcmp(code, prev) < 0);
            if (!before)
                break;
            memcpy(app->languages[at], app->languages[at - 1],
                   sizeof(app->languages[0]));
            at--;
        }
        snprintf(app->languages[at], sizeof(app->languages[at]), "%s", code);
        app->language_count++;
    }
    SDL_free(names);
}

/* The interface text and the cached language name for messages_lang(). */
static void load_interface_text(App *app)
{
    char path[APP_PATH_BUF], ierr[256] = "";
    itext_reset();
    messages_path(path, sizeof(path), app->data_dir, "interface.tsv");
    if (!itext_load(path, ierr, sizeof(ierr)))
        log_add(&app->log, "interface text: %s", ierr);
    messages_lang_name(app->data_dir, messages_lang(), app->language_name,
                       sizeof(app->language_name));
    /* A language the sans face cannot write (Japanese) is drawn wholly in
     * the system's font for it, Latin letters and digits included. */
    if (app->assets.latin_ui[UI_FONT_14])
        assets_use_cjk(&app->assets,
                       !assets_latin_can_draw(&app->assets, app->language_name));
    /* The intro's and ending's pictures, with their words in it. */
    pictures_bake(&app->assets, app->render.renderer,
                  app->assets.font != app->assets.latin_font);
}

const char *app_pick_language(const App *app, const char *const *prefs,
                              int count)
{
    for (int p = 0; p < count; p++) {
        if (!prefs[p])
            continue;
        char code[16];
        size_t n = 0;
        while (prefs[p][n] && prefs[p][n] != '_' && prefs[p][n] != '-' &&
               n < sizeof(code) - 1) {
            char c = prefs[p][n];
            code[n++] = (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        }
        code[n] = '\0';
        for (int i = 0; i < app->language_count; i++)
            if (n && strcmp(app->languages[i], code) == 0)
                return app->languages[i];
    }
    return MESSAGES_DEFAULT_LANG;
}

/* The system's preferred languages, in order (SDL_GetPreferredLocales). */
static const char *system_language(const App *app)
{
    int n = 0;
    SDL_Locale **locales = SDL_GetPreferredLocales(&n);
    const char *prefs[16];
    int count = 0;
    for (int i = 0; locales && i < n && count < 16; i++)
        if (locales[i] && locales[i]->language)
            prefs[count++] = locales[i]->language;
    const char *code = app_pick_language(app, prefs, count);
    SDL_free(locales);
    return code;
}

bool app_set_language(App *app, const char *code)
{
    if (!app || !code)
        return false;
    bool known = false;
    for (int i = 0; i < app->language_count; i++)
        if (strcmp(app->languages[i], code) == 0)
            known = true;
    if (!known || !messages_set_lang(code))
        return false;
    load_interface_text(app);
    events_load_text(&app->events);
    app_refresh_slots(app);
    snprintf(app->options.language, sizeof(app->options.language), "%s",
             code);
    save_options(app);
    return true;
}

void app_next_language(App *app)
{
    if (!app || app->language_count == 0)
        return;
    int at = 0;
    for (int i = 0; i < app->language_count; i++)
        if (strcmp(app->languages[i], messages_lang()) == 0)
            at = i;
    app_set_language(app, app->languages[(at + 1) % app->language_count]);
}

/* Days since 1970-01-01 of a proleptic Gregorian date. */
static long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void app_format_slot_time(const SDL_DateTime *saved, const SDL_DateTime *now,
                          SDL_DateFormat df, SDL_TimeFormat tf,
                          char *out, size_t outsz)
{
    if (!out || outsz == 0)
        return;
    out[0] = '\0';
    if (!saved || !now)
        return;
    char clock[16];
    if (tf == SDL_TIME_FORMAT_12HR) {
        int h = saved->hour % 12;
        snprintf(clock, sizeof(clock), "%d:%02d %s", h ? h : 12,
                 saved->minute, saved->hour < 12 ? "AM" : "PM");
    } else {
        snprintf(clock, sizeof(clock), "%02d:%02d", saved->hour, saved->minute);
    }
    long ago = days_from_civil(now->year, now->month, now->day) -
               days_from_civil(saved->year, saved->month, saved->day);
    bool later = ago < 0 ||
        (ago == 0 && (saved->hour * 60 + saved->minute) * 60 + saved->second >
                     (now->hour * 60 + now->minute) * 60 + now->second);
    if (!later && ago == 0) {
        snprintf(out, outsz, "%s %s", tx(ITX_SLOT_TODAY), clock);
        return;
    }
    if (!later && ago == 1) {
        snprintf(out, outsz, "%s %s", tx(ITX_SLOT_YESTERDAY), clock);
        return;
    }
    switch (df) {
    case SDL_DATE_FORMAT_YYYYMMDD:
        snprintf(out, outsz, "%04d-%02d-%02d %s", saved->year, saved->month,
                 saved->day, clock);
        break;
    case SDL_DATE_FORMAT_MMDDYYYY:
        snprintf(out, outsz, "%02d/%02d/%04d %s", saved->month, saved->day,
                 saved->year, clock);
        break;
    default:
        snprintf(out, outsz, "%02d.%02d.%04d %s", saved->day, saved->month,
                 saved->year, clock);
        break;
    }
}

void app_refresh_slots(App *app)
{
    if (!app) return;
    SDL_DateFormat df = SDL_DATE_FORMAT_DDMMYYYY;
    SDL_TimeFormat tf = SDL_TIME_FORMAT_24HR;
    SDL_GetDateTimeLocalePreferences(&df, &tf);
    SDL_Time now_t = 0;
    SDL_DateTime now;
    bool have_now = SDL_GetCurrentTime(&now_t) &&
                    SDL_TimeToDateTime(now_t, &now, true);
    app->slot_newest = -1;
    SDL_Time newest_time = 0;
    for (int slot = 0; slot <= UI_SLOT_COUNT; slot++) {
        app->slot_valid[slot] = false;
        app->slot_top[slot] = 0;
        app->slot_when[slot][0] = '\0';
        char path[APP_PATH_BUF];
        if (!app_save_slot_path(app, slot, path, sizeof(path)))
            continue;
        SaveData save;
        char err[128];
        if (!save_load_file_profile(path, &save, slot == 0, err, sizeof(err)))
            continue;
        app->slot_valid[slot] = true;
        app->slot_top[slot] = state_get(&save.state, ST_FLOOR_LIMIT);
        SDL_PathInfo info;
        if (!SDL_GetPathInfo(path, &info))
            continue;
        SDL_DateTime saved;
        if (have_now && SDL_TimeToDateTime(info.modify_time, &saved, true))
            app_format_slot_time(&saved, &now, df, tf, app->slot_when[slot],
                                 sizeof(app->slot_when[slot]));
        if (app->slot_newest < 0 || info.modify_time > newest_time) {
            app->slot_newest = slot;
            newest_time = info.modify_time;
        }
    }
}

void app_open_ranking(App *app)
{
    if (!app) return;
    if (event_engine_show_ranking(&app->events.engine, 1))
        ui_close_menu(&app->ui);
}

void app_pump_audio(App *app)
{
    if (!app)
        return;
    EventAudioCmd cmds[EVENT_ENGINE_AUDIO_QUEUE];
    int n = event_engine_take_audio(&app->events.engine, cmds,
                                    EVENT_ENGINE_AUDIO_QUEUE);
    for (int i = 0; i < n; i++) {
        switch (cmds[i].kind) {
        case EVENT_AUDIO_WAV:
            audio_play_sound(app->audio, cmds[i].arg);
            break;
        case EVENT_AUDIO_BGM:
            audio_play_bgm(app->audio, cmds[i].arg);
            break;
        case EVENT_AUDIO_BGM_STOP:
            audio_stop_bgm(app->audio);
            break;
        default:
            break;
        }
    }
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
    app_apply_sound_switches(app);      /* syokidata2's soundplay */
    app->step_active = false;
    ui_close_menu(&app->ui);
    log_add(&app->log, tx(ITX_LOG_NEW_GAME),
            state_get(&app->game.state, ST_LIFE),
            state_get(&app->game.state, ST_ATTACK),
            state_get(&app->game.state, ST_DEFENSE));
    return true;
}

bool app_can_save(const App *app)
{
    if (!app)
        return false;
    /* The original's Save submenu starts disabled (GameSave1 in the DFM)
     * and itemlive (0x450880) sets it from the orb: slot 0 == 1.  While it
     * is 2 the orb is being granted and itemlive leaves the menu alone. */
    return state_get(&app->game.state, ST_LIFE) > 0 &&
           state_get(&app->game.state,
                     ST_SPECIAL_FIRST + ITEM_SLOT_ORB_HERO) == 1;
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
    if (slot < 1 || !app_save_slot_path(app, slot, path, sizeof(path))) {
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
    log_add(&app->log, tx(ITX_LOG_SAVED_SLOT), slot, path);
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
    if (!save_load_file_profile(path, &save, slot == 0, err, errsz))
        return false;
    game_load(&app->game, &save);
    events_cancel(&app->events);
    app_apply_sound_switches(app);      /* loadwork's soundcheck/play */
    app->step_active = false;
    ui_close_menu(&app->ui);
    log_add(&app->log, tx(ITX_LOG_LOADED_SLOT),
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
    log_add(&app->log, tx(ITX_LOG_CHECKPOINT), path);
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
              bool no_intro, bool windowed, char *err, size_t errsz)
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
    options_defaults(&app->options);
    if (options_path(app->options_path, sizeof(app->options_path)))
        options_load(&app->options, app->options_path);
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

    if (!render_init(&app->render, TSW_TITLE,
                     err, errsz)) {
        cleanup_partial(app);
        return false;
    }
    if (!assets_init(&app->assets, app->render.renderer, data_dir,
                     font_path, err, errsz)) {
        cleanup_partial(app);
        return false;
    }
    /* The language options.ini names, if data/text/ has it; without one
     * (never chosen in the menu) the system's preferred language that
     * data/text/ has, else English.  Then the remake's interface text in
     * it (itext.h).  After the fonts: a language whose letters they cannot
     * draw is not offered. */
    app_scan_languages(app);
    messages_set_lang(MESSAGES_DEFAULT_LANG);
    bool chosen = false;
    for (int i = 0; i < app->language_count; i++)
        if (strcmp(app->languages[i], app->options.language) == 0)
            chosen = messages_set_lang(app->options.language);
    if (!chosen)
        messages_set_lang(system_language(app));
    load_interface_text(app);

    if (app->options.fullscreen && !windowed &&
        !SDL_SetWindowFullscreen(app->render.window, true))
        log_add(&app->log, "Fullscreen failed: %s", SDL_GetError());

    char icon_path[APP_PATH_BUF];
    snprintf(icon_path, sizeof(icon_path), "%s/gfx/Icon.png", data_dir);
    SDL_Surface *icon = IMG_Load(icon_path);
    if (icon) {
        if (!SDL_SetWindowIcon(app->render.window, icon))
            log_add(&app->log, "Window icon failed: %s", SDL_GetError());
        SDL_DestroySurface(icon);
    }

    char map_path[APP_PATH_BUF];
    char map_err[APP_PATH_BUF + 32] = "";
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
            log_add(&app->log, tx(ITX_LOG_SAVE_FILE), app->save_path,
                    state_status_string(save.status));
            log_add(&app->log, tx(ITX_LOG_SAVE_STATS),
                    game_floor(&app->game),
                    state_get(&app->game.state, ST_LIFE),
                    state_get(&app->game.state, ST_ATTACK),
                    state_get(&app->game.state, ST_DEFENSE),
                    state_get(&app->game.state, ST_GOLD));
        } else {
            log_add(&app->log, tx(ITX_LOG_SAVE_REJECTED), save_err);
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
        log_add(&app->log, tx(ITX_LOG_NEW_GAME),
                state_get(&app->game.state, ST_LIFE),
                state_get(&app->game.state, ST_ATTACK),
                state_get(&app->game.state, ST_DEFENSE));
    } else {
        log_add(&app->log, tx(ITX_LOG_LOADED_FLOOR), game_floor(&app->game));
    }

    log_add(&app->log, "%s", tx(ITX_LOG_HELP));

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

    app->events.engine.vm.speed = app->options.speed;
    app->events.engine.vm.interval_ms =
        event_vm_speed_interval_ms(app->options.speed);
    if (!getenv("TSW_NO_AUDIO"))
        app->audio = audio_init(data_dir);
    app_maybe_trigger_floor_event(app);
    if (!loaded_save && !no_intro)
        app_start_intro(app);
    return true;
}

void app_run(App *app)
{
    if (!app) return;
    while (app->running) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            input_handle_event(app, &event);
        app_update_intro(app);
        input_update(app);
        events_update(&app->events, app_now(app));
        app_pump_audio(app);
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
