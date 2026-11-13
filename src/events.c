#include "events.h"

#include "app.h"
#include "state.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

static void runtime_log(const EventRuntime *events, const char *message)
{
    if (!message)
        return;
    if (events && events->log_fn)
        events->log_fn(events->log_user, message);
    else
        printf("%s\n", message);
}

bool events_file_exists(const char *path)
{
    if (!path || !*path)
        return false;
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    fclose(f);
    return true;
}

static int facing_to_direction(int facing)
{
    /* ST_FACING 1=down 2=left 3=right 4=up -> g_move_dir 1=up 2=left
     * 3=right 4=down (17-static-findings.md section 7). */
    switch (facing) {
    case 1: return 4;
    case 2: return 2;
    case 3: return 3;
    default: return 1;
    }
}

bool events_trigger(EventRuntime *events, int floor, int index, uint8_t raw)
{
    if (!events || event_engine_active(&events->engine))
        return false;
    if (events->app) {
        int facing = (int)state_get(&events->app->game.state, ST_FACING);
        int old_x = game_x(&events->app->game);
        int old_y = game_y(&events->app->game);
        EventEngineResult r = event_engine_trigger(
            &events->engine, floor, index, raw, facing_to_direction(facing));
        if (r == EVENT_ENGINE_STARTED) {
            if (events->engine.player_step_pending) {
                /* idou already wrote the destination tile; animate the
                 * player from the pre-trigger tile instead of jumping. */
                events->engine.player_step_pending = false;
                int new_x = game_x(&events->app->game);
                int new_y = game_y(&events->app->game);
                if (new_x != old_x || new_y != old_y) {
                    events->app->step_active = true;
                    events->app->step_from_x = (float)old_x;
                    events->app->step_from_y = (float)old_y;
                    events->app->step_start_ms = app_now(events->app);
                }
            }
            state_compute_checksums(&events->app->game.state);
            if (events->engine.checkpoint_pending) {
                /* yousei: save0.dat with the map and state as they are
                 * now (score, rank and area already set). */
                char err[256] = "";
                events->engine.checkpoint_pending = false;
                if (!app_checkpoint_save(events->app, err, sizeof(err))) {
                    char msg[300];
                    snprintf(msg, sizeof(msg), "checkpoint: %s", err);
                    runtime_log(events, msg);
                }
            }
            return true;
        }
        events->engine.player_step_pending = false;
    }
    /* Not routed to a builder, or failed closed with its inputs restored. */
    return false;
}

bool events_after_battle(EventRuntime *events)
{
    if (!events)
        return false;
    bool started = event_engine_after_battle(&events->engine) ==
                   EVENT_ENGINE_STARTED && event_engine_active(&events->engine);
    if (started && events->app)
        state_compute_checksums(&events->app->game.state);
    return started;
}

bool events_after_step(EventRuntime *events)
{
    if (!events)
        return false;
    bool started = event_engine_after_step(&events->engine) ==
                   EVENT_ENGINE_STARTED;
    if (events->app)
        state_compute_checksums(&events->app->game.state);
    return started && event_engine_active(&events->engine);
}

bool events_active(const EventRuntime *events)
{
    return events && event_engine_active(&events->engine);
}

void events_cancel(EventRuntime *events)
{
    if (!events)
        return;
    event_engine_cancel(&events->engine);
}

void events_update(EventRuntime *events, uint64_t now)
{
    if (!events)
        return;
    if (!event_engine_active(&events->engine)) {
        /* Timeline done: its actors and doors go with it. */
        event_engine_clear_actors(&events->engine);
        return;
    }
    bool drained = event_engine_update(&events->engine, now);
    if (events->engine.resets != events->seen_resets) {
        /* op 0x16 restarted the game (try again): floor 1, new stats. */
        events->seen_resets = events->engine.resets;
        if (events->app) {
            events->app->step_active = false;
            log_add(&events->app->log, "Try again: floor 1, HP %d",
                    state_get(&events->app->game.state, ST_LIFE));
        }
        drained = true;
    }
    if (drained && events->app)
        state_compute_checksums(&events->app->game.state);
}

bool events_handle_key(EventRuntime *events, int key, bool repeat)
{
    if (!events)
        return false;
    /* Ranking window (ShowModal): Enter/Space/Escape close it. */
    if (events->engine.ranking_open) {
        if (!repeat && (key == SDLK_RETURN || key == SDLK_KP_ENTER ||
                        key == SDLK_SPACE || key == SDLK_ESCAPE))
            event_engine_close_ranking(&events->engine);
        return true;
    }
    /* Altar menu: Up/Down move the focus over the four options,
     * Enter/Space picks it, Escape cancels (Button42). */
    if (events->engine.altar_open) {
        EventEngine *ae = &events->engine;
        int pick = -1;
        if (key == SDLK_UP)
            ae->altar_focus = (ae->altar_focus + 3) % 4;
        else if (key == SDLK_DOWN || key == SDLK_TAB)
            ae->altar_focus = (ae->altar_focus + 1) % 4;
        else if (!repeat && (key == SDLK_RETURN || key == SDLK_KP_ENTER ||
                             key == SDLK_SPACE))
            pick = ae->altar_focus;
        else if (!repeat && key == SDLK_ESCAPE)
            pick = 3;
        if (pick >= 0 && event_engine_altar_choose(ae, pick) && events->app)
            state_compute_checksums(&events->app->game.state);
        return true;
    }
    /* Merchant question: Left/Right/Tab move the focus between Yes
     * (Button2) and No (Button7), Enter/Space presses it, Y/N answer
     * directly, Escape is No when it is enabled. */
    EventEngine *en = &events->engine;
    if (en->dialog_open && en->dialog_choice) {
        int answer = -1;               /* 1 yes, 0 no */
        if (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_TAB) {
            if (en->choice_no_enabled)
                en->choice_focus_no = !en->choice_focus_no;
        } else if (repeat) {
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER ||
                   key == SDLK_SPACE) {
            answer = en->choice_focus_no ? 0 : 1;
        } else if (key == SDLK_Y) {
            answer = 1;
        } else if (key == SDLK_N || key == SDLK_ESCAPE) {
            answer = 0;
        }
        if (answer >= 0 && event_engine_choose(en, answer == 1) &&
            events->app)
            state_compute_checksums(&events->app->game.state);
        return true;
    }
    /* zyouout's "try again?" MessageDlg (mbYesNo): same keys as the
     * merchant question; Escape is No. */
    if (en->death_prompt) {
        int answer = -1;
        if (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_TAB) {
            en->choice_focus_no = !en->choice_focus_no;
        } else if (repeat) {
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER ||
                   key == SDLK_SPACE) {
            answer = en->choice_focus_no ? 0 : 1;
        } else if (key == SDLK_Y) {
            answer = 1;
        } else if (key == SDLK_N || key == SDLK_ESCAPE) {
            answer = 0;
        }
        if (answer >= 0) {
            en->choice_focus_no = false;
            event_engine_death_answer(en, answer == 1);
            /* No: the original only enables Game > Load; the remake's
             * game-over screen offers the way out. */
            if (answer == 0 && events->app)
                ui_open_game_over(&events->app->ui);
        }
        return true;
    }
    /* zyouout window: Enter/Space confirms (Button1Click). */
    if (events->engine.dialog_open && !repeat &&
        (key == SDLK_RETURN || key == SDLK_KP_ENTER ||
         key == SDLK_SPACE)) {
        event_engine_confirm(&events->engine);
        if (events->app)
            state_compute_checksums(&events->app->game.state);
    }
    return event_engine_active(&events->engine);  /* input locked */
}

bool events_runtime_init(EventRuntime *events, struct App *app,
                         const char *builders_path,
                         EventsLogFn log_fn, void *user,
                         char *err, size_t errsz)
{
    if (!events || !builders_path || !*builders_path) {
        if (err && errsz) snprintf(err, errsz, "missing event builders");
        return false;
    }
    memset(events, 0, sizeof(*events));
    events->app = app;
    events->log_fn = log_fn;
    events->log_user = user;

    events->engine.log_fn = log_fn;
    events->engine.log_user = user;
    char engine_err[512] = "";
    if (!event_engine_init(&events->engine, builders_path, engine_err,
                           sizeof(engine_err))) {
        char msg[640];
        snprintf(msg, sizeof(msg), "event builders unavailable: %s",
                 engine_err);
        runtime_log(events, msg);
    }
    if (app) {
        event_engine_bind(&events->engine, &app->game.state, &app->game.map);
        if (app->have_initial_map)
            event_engine_set_initial_map(&events->engine, &app->initial_map);
        /* The "try again?" MessageDlg literal: ui.tsv entry 1. */
        char ui_path[1100];
        snprintf(ui_path, sizeof(ui_path), "%s/text/ui.tsv", app->data_dir);
        EventEngine *en = &events->engine;
        en->have_ui_texts = messages_load(&en->ui_texts, ui_path, NULL, 0);
        if (en->have_ui_texts) {
            const char *t = messages_get(&en->ui_texts, 1);
            if (t)
                snprintf(en->death_prompt_text,
                         sizeof(en->death_prompt_text), "%s", t);
        }
        /* Dialogue text: data/text/messages.tsv (tools/extract_messages.py). */
        char messages[1100];
        snprintf(messages, sizeof(messages), "%s/text/messages.tsv",
                 app->data_dir);
        char msg_err[512] = "";
        if (!event_engine_load_messages(&events->engine, messages, msg_err,
                                        sizeof(msg_err))) {
            char msg[700];
            snprintf(msg, sizeof(msg),
                     "dialogue text unavailable (%s); run "
                     "tools/extract_messages.py", msg_err);
            runtime_log(events, msg);
        }
    }
    return true;
}

void events_runtime_shutdown(EventRuntime *events)
{
    if (!events)
        return;
    events_cancel(events);
    event_engine_shutdown(&events->engine);
}
