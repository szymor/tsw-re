#include "events.h"
#include "itext.h"

#include "app.h"
#include "fileio.h"
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
    return path_is_file(path);
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
    bool want_text = events->engine.ranking_name_entry;
    if (want_text != events->text_input_on && events->app &&
        events->app->render.window) {
        if (want_text) {
            /* The input method's candidate list beside the name box
             * (render_windows.c: the edit field at 182, 208, 276 x 24). */
            SDL_Renderer *r = events->app->render.renderer;
            float x = 182.0f, y = 208.0f, w = 276.0f, h = 24.0f;
            float wx = x, wy = y, wx2 = x + w, wy2 = y + h;
            if (r) {
                SDL_RenderCoordinatesToWindow(r, x, y, &wx, &wy);
                SDL_RenderCoordinatesToWindow(r, x + w, y + h, &wx2, &wy2);
            }
            SDL_Rect area = { (int)wx, (int)wy, (int)(wx2 - wx),
                              (int)(wy2 - wy) };
            SDL_SetTextInputArea(events->app->render.window, &area, 0);
            events->name_edit[0] = '\0';
            SDL_StartTextInput(events->app->render.window);
        } else {
            SDL_StopTextInput(events->app->render.window);
        }
        events->text_input_on = want_text;
    }
    if (now >= events->next_timer1) {
        /* Timer1Timer runs beside the timelines at the speed interval
         * (speedhigh/middle/low set Timer1 with Timer2, 0x47f4e3). */
        events->next_timer1 =
            now + (uint64_t)event_vm_speed_interval_ms(events->engine.vm.speed);
        int f15 = events->engine.vm.state ? events->engine.vm.state->raw[0x98] : 0;
        int f35 = events->engine.vm.state ? events->engine.vm.state->raw[0x99] : 0;
        event_engine_timer1(&events->engine);
        if (events->app && (events->engine.vm.state->raw[0x98] != f15 ||
                            events->engine.vm.state->raw[0x99] != f35))
            state_compute_checksums(&events->app->game.state);
    }
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
            log_add(&events->app->log, tx(ITX_LOG_TRY_AGAIN),
                    state_get(&events->app->game.state, ST_LIFE));
        }
        drained = true;
    }
    if (drained && events->app)
        state_compute_checksums(&events->app->game.state);
}

/* Form2 rewrote save0.dat from its copy of the state with the fairy's
 * score, rank and area (0x55..0x57) cleared (FUN_00439ce0). */
static void ranking_checkpoint(EventRuntime *events)
{
    EventEngine *e = &events->engine;
    if (!e->ranking_checkpoint_pending)
        return;
    e->ranking_checkpoint_pending = false;
    if (!events->app)
        return;
    StateBlock *s = &events->app->game.state;
    int32_t keep[3] = { s->raw[0x55], s->raw[0x56], s->raw[0x57] };
    s->raw[0x55] = s->raw[0x56] = s->raw[0x57] = 0;
    state_compute_checksums(s);
    char err[256] = "";
    if (!app_checkpoint_save(events->app, err, sizeof(err))) {
        char msg[300];
        snprintf(msg, sizeof(msg), "checkpoint: %s", err);
        runtime_log(events, msg);
    }
    s->raw[0x55] = keep[0]; s->raw[0x56] = keep[1]; s->raw[0x57] = keep[2];
    state_compute_checksums(s);
}

static size_t utf8_chars(const char *s)
{
    size_t n = 0;
    for (; *s; s++)
        if (((unsigned char)*s & 0xc0) != 0x80)
            n++;
    return n;
}

/* `src` into `dst` (size `size`) without cutting a character in half. */
static void copy_whole_chars(char *dst, size_t size, const char *src)
{
    size_t n = strlen(src);
    if (n >= size) {
        n = size - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xc0) == 0x80)
            n--;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void events_handle_text(EventRuntime *events, const char *text)
{
    if (!events || !text || !events->engine.ranking_name_entry)
        return;
    events->name_edit[0] = '\0';           /* the input method confirmed */
    /* Whole characters only, at most EVENTS_NAME_CHARS of them, and none
     * the fonts cannot draw (they would show as boxes in the ranking). */
    size_t len = strlen(events->name_input);
    if (len + strlen(text) >= sizeof(events->name_input) ||
        utf8_chars(events->name_input) + utf8_chars(text) > EVENTS_NAME_CHARS)
        return;
    if (events->app && events->app->assets.ui[UI_FONT_14] &&
        !assets_can_draw(&events->app->assets, text))
        return;
    memcpy(events->name_input + len, text, strlen(text) + 1);
}

void events_handle_editing(EventRuntime *events, const char *text)
{
    if (!events || !text || !events->engine.ranking_name_entry)
        return;
    copy_whole_chars(events->name_edit, sizeof(events->name_edit), text);
}

bool events_handle_key(EventRuntime *events, int key, bool repeat)
{
    if (!events)
        return false;
    EventEngine *me = &events->engine;
    /* ShowMessage: Enter/Space/Escape press OK. */
    if (me->message_open) {
        if (!repeat && (key == SDLK_RETURN || key == SDLK_KP_ENTER ||
                        key == SDLK_SPACE || key == SDLK_ESCAPE))
            event_engine_close_message(me);
        return true;
    }
    /* Form2's InputQuery: typing, Backspace, Enter (OK), Escape (Cancel). */
    if (me->ranking_name_entry) {
        if (key == SDLK_BACKSPACE) {
            size_t len = strlen(events->name_input);
            while (len > 0 &&
                   ((unsigned char)events->name_input[len - 1] & 0xc0) == 0x80)
                len--;                          /* UTF-8 continuation */
            if (len > 0)
                len--;
            events->name_input[len] = '\0';
        } else if (!repeat && (key == SDLK_RETURN || key == SDLK_KP_ENTER ||
                               key == SDLK_ESCAPE)) {
            event_engine_ranking_name(me, events->name_input,
                                      key != SDLK_ESCAPE);
            events->name_input[0] = '\0';
            events->name_edit[0] = '\0';
            ranking_checkpoint(events);
        }
        return true;
    }
    /* Ranking window (ShowModal): Left/Right show the other areas (the
     * area buttons), Enter/Space/Escape close it. */
    if (events->engine.ranking_open) {
        if (key == SDLK_LEFT || key == SDLK_RIGHT) {
            event_engine_ranking_browse(&events->engine,
                                        key == SDLK_LEFT ? -1 : 1);
            return true;
        }
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
        else if (!repeat && (key == SDLK_1 || key == SDLK_2 || key == SDLK_3))
            pick = (int)(key - SDLK_1);         /* the three offers */
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
        if (key == SDLK_LEFT) {
            en->choice_focus_no = false;
        } else if (key == SDLK_RIGHT) {
            en->choice_focus_no = en->choice_no_enabled;
        } else if (key == SDLK_TAB) {
            if (en->choice_no_enabled)
                en->choice_focus_no = !en->choice_focus_no;
        } else if (repeat) {
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER ||
                   key == SDLK_SPACE) {
            answer = en->choice_focus_no ? 0 : 1;
        } else if (key == itext_keycode(ITX_KEY_YES)) {
            answer = 1;
        } else if (key == itext_keycode(ITX_KEY_NO) ||
                   key == SDLK_ESCAPE) {
            answer = 0;
        }
        if (answer >= 0 && event_engine_choose(en, answer == 1) &&
            events->app)
            state_compute_checksums(&events->app->game.state);
        return true;
    }
    /* The Orb of Wisdom's list: Up/Down (or Left/Right) move over the
     * people heard and show their words; Enter/Space/Escape close it (OK,
     * Button35). */
    if (en->wisdom_open) {
        if (key == SDLK_UP || key == SDLK_LEFT)
            event_engine_wisdom_move(en, -1);
        else if (key == SDLK_DOWN || key == SDLK_RIGHT || key == SDLK_TAB)
            event_engine_wisdom_move(en, 1);
        else if (!repeat && (key == SDLK_RETURN || key == SDLK_KP_ENTER ||
                             key == SDLK_SPACE || key == SDLK_ESCAPE))
            event_engine_wisdom_press(en, 24);
        if (events->app)
            state_compute_checksums(&events->app->game.state);
        return true;
    }
    /* The ending's last picture (Image34): Enter/Space click it. */
    if (en->ending_wait) {
        if (!repeat && (key == SDLK_RETURN || key == SDLK_KP_ENTER ||
                        key == SDLK_SPACE))
            event_engine_ending_continue(en);
        return true;
    }
    /* Item panel (Button38 Yes / Button37 No): same keys as the merchant
     * question; Escape is No. */
    if (en->item_confirm) {
        int answer = -1;
        if (key == SDLK_LEFT) {
            en->item_confirm_focus_no = false;         /* Yes */
        } else if (key == SDLK_RIGHT) {
            en->item_confirm_focus_no = true;          /* No */
        } else if (key == SDLK_TAB || key == SDLK_UP || key == SDLK_DOWN) {
            en->item_confirm_focus_no = !en->item_confirm_focus_no;
        } else if (repeat) {
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER ||
                   key == SDLK_SPACE) {
            answer = en->item_confirm_focus_no ? 0 : 1;
        } else if (key == itext_keycode(ITX_KEY_YES)) {
            answer = 1;
        } else if (key == itext_keycode(ITX_KEY_NO) ||
                   key == SDLK_ESCAPE) {
            answer = 0;
        }
        if (answer >= 0 && event_engine_item_answer(en, answer == 1) &&
            events->app) {
            events->app->step_active = false;
            state_compute_checksums(&events->app->game.state);
        }
        return true;
    }
    /* Orb of Flying: Up (Button8) / Down (Button9) change the floor shown,
     * Enter/Space/Escape land there (Button10). */
    if (en->fly_open) {
        int action = 2;
        if (key == SDLK_UP)
            action = 1;
        else if (key == SDLK_DOWN)
            action = -1;
        else if (!repeat && (key == SDLK_RETURN || key == SDLK_KP_ENTER ||
                             key == SDLK_SPACE || key == SDLK_ESCAPE))
            action = 0;
        if (action != 2 && event_engine_fly(en, action) && events->app) {
            events->app->step_active = false;
            state_compute_checksums(&events->app->game.state);
        }
        return true;
    }
    /* zyouout's "try again?" MessageDlg (mbYesNo): same keys as the
     * merchant question; Escape is No. */
    if (en->death_prompt) {
        int answer = -1;
        if (key == SDLK_LEFT) {
            en->choice_focus_no = false;
        } else if (key == SDLK_RIGHT) {
            en->choice_focus_no = true;
        } else if (key == SDLK_TAB) {
            en->choice_focus_no = !en->choice_focus_no;
        } else if (repeat) {
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER ||
                   key == SDLK_SPACE) {
            answer = en->choice_focus_no ? 0 : 1;
        } else if (key == itext_keycode(ITX_KEY_YES)) {
            answer = 1;
        } else if (key == itext_keycode(ITX_KEY_NO) ||
                   key == SDLK_ESCAPE) {
            answer = 0;
        }
        if (answer >= 0) {
            /* No leaves the death screen in place: the original only
             * enables Game > Load and stops there.  The remake keeps the
             * map with the menu usable (input.c blocks the dead hero). */
            event_engine_death_answer(en, answer == 1);
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

int events_use_item(EventRuntime *events, int slot)
{
    if (!events)
        return EVENT_ENGINE_NOT_ROUTED;
    EventEngineResult r = event_engine_use_item(&events->engine, slot);
    if (r == EVENT_ENGINE_STARTED && events->app)
        state_compute_checksums(&events->app->game.state);
    return r;
}

/* The game text in the current language (messages.h messages_lang):
 * ui.tsv, messages.tsv and speakers.tsv, at start and when the Options
 * menu changes the language. */
void events_load_text(EventRuntime *events)
{
    App *app = events ? events->app : NULL;
    if (!app)
        return;
    /* The "try again?" MessageDlg literal: ui.tsv entry 1. */
    char ui_path[1100];
    messages_path(ui_path, sizeof(ui_path), app->data_dir, "ui.tsv");
    EventEngine *en = &events->engine;
    if (en->have_ui_texts)
        messages_free(&en->ui_texts);
    en->death_prompt_text[0] = '\0';
    en->have_ui_texts = messages_load(&en->ui_texts, ui_path, NULL, 0);
    if (en->have_ui_texts) {
        const char *t = messages_get(&en->ui_texts, 1);
        if (t)
            snprintf(en->death_prompt_text,
                     sizeof(en->death_prompt_text), "%s", t);
    }
    /* Dialogue text: data/text/<lang>/messages.tsv. */
    char messages[1100];
    messages_path(messages, sizeof(messages), app->data_dir,
                  "messages.tsv");
    char msg_err[512] = "";
    if (!event_engine_load_messages(&events->engine, messages, msg_err,
                                    sizeof(msg_err))) {
        char msg[700];
        snprintf(msg, sizeof(msg),
                 "dialogue text unavailable (%s); run "
                 "tools/extract_messages.py", msg_err);
        runtime_log(events, msg);
    }
    /* The speakers' names in the same language. */
    char speakers[1100];
    messages_path(speakers, sizeof(speakers), app->data_dir,
                  "speakers.tsv");
    if (!event_engine_load_speakers(&events->engine, speakers))
        runtime_log(events, "speakers.tsv unavailable or malformed; "
                            "using the English speaker names");
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
        /* optionN.dat / optionN1.dat beside the saves. */
        snprintf(events->engine.ranking_dir, sizeof(events->engine.ranking_dir),
                 "%s", app->save_dir);
        events_load_text(events);
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
