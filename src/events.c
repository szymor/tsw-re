#include "events.h"

#include "app.h"
#include "battle.h"
#include "game.h"
#include "items.h"
#include "map.h"
#include "state.h"

#include <SDL3/SDL.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <stdio.h>
#include <string.h>

static EventRuntime *g_rt;

static void runtime_log(const char *message)
{
    if (!message)
        return;
    if (g_rt && g_rt->log_fn)
        g_rt->log_fn(g_rt->log_user, message);
    else
        printf("%s\n", message);
}

static int l_message(lua_State *L)
{
    const char *text = luaL_checkstring(L, 1);
    if (!g_rt)
        return 0;
    snprintf(g_rt->message, sizeof(g_rt->message), "%s", text);
    g_rt->wait = EVENT_WAIT_MESSAGE;
    return lua_yield(L, 0);
}

static int l_choice(lua_State *L)
{
    if (!g_rt)
        return 0;
    const char *prompt = luaL_checkstring(L, 1);
    snprintf(g_rt->choice_prompt, sizeof(g_rt->choice_prompt), "%s", prompt);
    int n = lua_gettop(L) - 1;
    if (n > EVENT_MAX_OPTIONS) n = EVENT_MAX_OPTIONS;
    g_rt->choice_count = n;
    for (int i = 0; i < n; i++) {
        const char *opt = luaL_checkstring(L, i + 2);
        snprintf(g_rt->choice_options[i], EVENT_OPTION_LEN, "%s", opt);
    }
    g_rt->choice_selected = 0;
    g_rt->wait = EVENT_WAIT_CHOICE;
    return lua_yield(L, 0);
}

static int l_wait(lua_State *L)
{
    if (!g_rt)
        return 0;
    int ticks = (int)luaL_checkinteger(L, 1);
    if (ticks < 0) ticks = 0;
    g_rt->wait = EVENT_WAIT_TICKS;
    g_rt->wait_until = app_now(g_rt->app) + (uint64_t)ticks * 16u;
    return lua_yield(L, 0);
}

static void clear_actors(EventRuntime *events)
{
    if (!events) return;
    memset(events->actors, 0, sizeof(events->actors));
}

static int l_spawn_actor(lua_State *L)
{
    if (!g_rt) return 0;
    int sprite = (int)luaL_checkinteger(L, 1);
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);
    if (x < 0 || x >= MAP_TILE_COLS || y < 0 || y >= MAP_TILE_ROWS)
        return 0;
    for (int i = 0; i < EVENT_MAX_ACTORS; i++) {
        if (!g_rt->actors[i].active) {
            g_rt->actors[i].active = true;
            g_rt->actors[i].sprite = sprite;
            g_rt->actors[i].x = x;
            g_rt->actors[i].y = y;
            lua_pushinteger(L, i + 1);
            return 1;
        }
    }
    return 0;
}

static int l_move_actor(lua_State *L)
{
    if (!g_rt) return 0;
    int id = (int)luaL_checkinteger(L, 1) - 1;
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);
    if (id >= 0 && id < EVENT_MAX_ACTORS && g_rt->actors[id].active) {
        g_rt->actors[id].x = x;
        g_rt->actors[id].y = y;
    }
    return 0;
}

static int l_remove_actor(lua_State *L)
{
    if (!g_rt) return 0;
    int id = (int)luaL_checkinteger(L, 1) - 1;
    if (id >= 0 && id < EVENT_MAX_ACTORS)
        g_rt->actors[id].active = false;
    return 0;
}

static int l_cutscene(lua_State *L)
{
    if (!g_rt) return 0;
    const char *text = luaL_checkstring(L, 1);
    int ticks = (int)luaL_checkinteger(L, 2);
    if (ticks < 0) ticks = 0;
    snprintf(g_rt->cutscene_text, sizeof(g_rt->cutscene_text), "%s", text);
    g_rt->cutscene_image = 0;
    g_rt->wait = EVENT_WAIT_CUTSCENE;
    g_rt->wait_until = app_now(g_rt->app) + (uint64_t)ticks * 16u;
    return lua_yield(L, 0);
}

static int l_cutscene_image(lua_State *L)
{
    if (!g_rt) return 0;
    int image = (int)luaL_checkinteger(L, 1);
    int ticks = (int)luaL_checkinteger(L, 2);
    if (ticks < 0) ticks = 0;
    if (image < 1 || image > 3) image = 0;
    g_rt->cutscene_image = image;
    g_rt->cutscene_text[0] = '\0';
    g_rt->wait = EVENT_WAIT_CUTSCENE;
    g_rt->wait_until = app_now(g_rt->app) + (uint64_t)ticks * 16u;
    return lua_yield(L, 0);
}

static void add_stat(int index, lua_Integer delta)
{
    if (!g_rt) return;
    if (delta > INT32_MAX) delta = INT32_MAX;
    if (delta < INT32_MIN) delta = INT32_MIN;
    int64_t value = (int64_t)state_get(&g_rt->app->game.state, index) +
                    (int64_t)delta;
    if (value > INT32_MAX) value = INT32_MAX;
    if (value < INT32_MIN) value = INT32_MIN;
    state_set(&g_rt->app->game.state, index, (int32_t)value);
    state_compute_checksums(&g_rt->app->game.state);
}

static int l_add_gold(lua_State *L)
{
    add_stat(ST_GOLD, luaL_checkinteger(L, 1));
    return 0;
}

static int l_heal(lua_State *L)
{
    add_stat(ST_LIFE, luaL_checkinteger(L, 1));
    return 0;
}

static int l_damage(lua_State *L)
{
    lua_Integer amount = luaL_checkinteger(L, 1);
    if (amount < 0) amount = 0;
    if (amount > INT32_MAX) amount = INT32_MAX;
    add_stat(ST_LIFE, -amount);
    return 0;
}

static int l_get_state(lua_State *L)
{
    if (!g_rt) { lua_pushinteger(L, 0); return 1; }
    int index = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, state_get(&g_rt->app->game.state, index));
    return 1;
}

static int l_set_state(lua_State *L)
{
    if (!g_rt) return 0;
    int index = (int)luaL_checkinteger(L, 1);
    lua_Integer value = luaL_checkinteger(L, 2);
    if (value > INT32_MAX) value = INT32_MAX;
    if (value < INT32_MIN) value = INT32_MIN;
    state_set(&g_rt->app->game.state, index, (int32_t)value);
    state_compute_checksums(&g_rt->app->game.state);
    return 0;
}

static int l_add_state(lua_State *L)
{
    if (!g_rt) return 0;
    int index = (int)luaL_checkinteger(L, 1);
    add_stat(index, luaL_checkinteger(L, 2));
    return 0;
}

static int l_set_tile(lua_State *L)
{
    if (!g_rt) return 0;
    int index = (int)luaL_checkinteger(L, 1);
    int value = (int)luaL_checkinteger(L, 2);
    int floor = game_floor(&g_rt->app->game);
    if (index >= 0 && index < MAP_TILES && floor >= 1 && floor <= 50) {
        g_rt->app->game.map.bytes[map_floor_offset(floor) + index] =
            (uint8_t)value;
    }
    return 0;
}

static int l_teleport(lua_State *L)
{
    if (!g_rt) return 0;
    int index = (int)luaL_checkinteger(L, 1);
    if (index >= 0 && index < MAP_TILES) {
        state_set(&g_rt->app->game.state, ST_X, index % MAP_TILE_COLS);
        state_set(&g_rt->app->game.state, ST_Y, index / MAP_TILE_COLS);
        state_compute_checksums(&g_rt->app->game.state);
    }
    return 0;
}

static int l_change_floor(lua_State *L)
{
    if (!g_rt) return 0;
    int floor = (int)luaL_checkinteger(L, 1);
    if (floor < 1) floor = 1;
    if (floor > 50) floor = 50;
    state_set(&g_rt->app->game.state, ST_FLOOR, floor);
    event_engine_clear_actors(&g_rt->engine);  /* VM actors belong to the floor */
    if (floor > state_get(&g_rt->app->game.state, ST_FLOOR_LIMIT))
        state_set(&g_rt->app->game.state, ST_FLOOR_LIMIT, floor);
    state_compute_checksums(&g_rt->app->game.state);
    return 0;
}

static int l_battle(lua_State *L)
{
    if (!g_rt) { lua_pushboolean(L, 0); return 1; }
    int index = (int)luaL_checkinteger(L, 1);
    char msg[256] = "";
    GameMoveResult result = game_fight(&g_rt->app->game, index,
                                       msg, sizeof(msg));
    if (msg[0])
        runtime_log(msg);
    lua_pushboolean(L, result == GAME_MOVE_BATTLE_WIN);
    return 1;
}

static int l_checkpoint(lua_State *L)
{
    if (!g_rt) { lua_pushboolean(L, 0); return 1; }
    char err[256] = "";
    bool ok = app_checkpoint_save(g_rt->app, err, sizeof(err));
    if (!ok)
        runtime_log(err);
    lua_pushboolean(L, ok);
    return 1;
}

static int l_rank_score(lua_State *L)
{
    if (!g_rt) { lua_pushinteger(L, 0); return 1; }
    int64_t score = app_rank_score(g_rt->app);
    char msg[128];
    snprintf(msg, sizeof(msg), "Provisional area-1 score: %lld",
             (long long)score);
    runtime_log(msg);
    lua_pushinteger(L, (lua_Integer)score);
    return 1;
}

static int l_sound(lua_State *L) { (void)L; return 0; }
static int l_sprite(lua_State *L) { (void)L; return 0; }
static int l_door(lua_State *L) { (void)L; return 0; }

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

static void register_api(lua_State *L)
{
    lua_register(L, "message", l_message);
    lua_register(L, "choice", l_choice);
    lua_register(L, "wait", l_wait);
    lua_register(L, "spawn_actor", l_spawn_actor);
    lua_register(L, "move_actor", l_move_actor);
    lua_register(L, "remove_actor", l_remove_actor);
    lua_register(L, "cutscene", l_cutscene);
    lua_register(L, "cutscene_image", l_cutscene_image);
    lua_register(L, "add_gold", l_add_gold);
    lua_register(L, "heal", l_heal);
    lua_register(L, "damage", l_damage);
    lua_register(L, "get_state", l_get_state);
    lua_register(L, "set_state", l_set_state);
    lua_register(L, "add_state", l_add_state);
    lua_register(L, "set_tile", l_set_tile);
    lua_register(L, "teleport", l_teleport);
    lua_register(L, "change_floor", l_change_floor);
    lua_register(L, "battle", l_battle);
    lua_register(L, "checkpoint", l_checkpoint);
    lua_register(L, "rank_score", l_rank_score);
    lua_register(L, "sound", l_sound);
    lua_register(L, "sprite", l_sprite);
    lua_register(L, "door", l_door);
}

static lua_State *event_thread(EventRuntime *events)
{
    if (!events || !events->L || events->thread_ref == LUA_NOREF)
        return NULL;
    lua_State *L = events->L;
    lua_rawgeti(L, LUA_REGISTRYINDEX, events->thread_ref);
    lua_State *co = lua_tothread(L, -1);
    lua_pop(L, 1);
    return co;
}

static void event_resume(EventRuntime *events, int nargs)
{
    if (!events || !events->active || !events->L)
        return;
    lua_State *L = events->L;
    lua_State *co = event_thread(events);
    if (!co) {
        events_cancel(events);
        return;
    }
    events->wait = EVENT_WAIT_NONE;
    int nresults = 0;
    int rc = lua_resume(co, L, nargs, &nresults);
    if (rc == LUA_OK) {
        luaL_unref(L, LUA_REGISTRYINDEX, events->thread_ref);
        events->thread_ref = LUA_NOREF;
        events->active = false;
        events->wait = EVENT_WAIT_NONE;
    } else if (rc == LUA_YIELD) {
        if (events->wait == EVENT_WAIT_NONE) {
            runtime_log("Lua event yielded without a wait request");
            events_cancel(events);
        }
    } else {
        const char *message = lua_tostring(co, -1);
        runtime_log(message ? message : "Lua event error");
        events_cancel(events);
    }
    if (events->app &&
        state_get(&events->app->game.state, ST_LIFE) <= 0) {
        events_cancel(events);
        ui_open_game_over(&events->app->ui);
    }
}

static bool start_key(EventRuntime *events, const char *key)
{
    if (!events || !events->L || events->active)
        return false;
    lua_State *L = events->L;
    lua_getglobal(L, "events");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    lua_getfield(L, -1, key);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 2);
        return false;
    }
    lua_State *co = lua_newthread(L);
    lua_pushvalue(L, -2);
    lua_xmove(L, co, 1);
    events->thread_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pop(L, 2);
    events->active = true;
    events->wait = EVENT_WAIT_NONE;
    events->message[0] = '\0';
    events->choice_prompt[0] = '\0';
    events->choice_count = 0;
    events->cutscene_text[0] = '\0';
    events->cutscene_image = 0;
    clear_actors(events);
    event_resume(events, 0);
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
    if (!events || events->active || event_engine_active(&events->engine))
        return false;
    if (events->app) {
        int facing = (int)state_get(&events->app->game.state, ST_FACING);
        EventEngineResult r = event_engine_trigger(
            &events->engine, floor, index, raw, facing_to_direction(facing));
        if (r == EVENT_ENGINE_STARTED) {
            state_compute_checksums(&events->app->game.state);
            return true;
        }
        if (r == EVENT_ENGINE_FAILED)
            return false;   /* failed closed; inputs already restored */
    }
    char key[64];
    snprintf(key, sizeof(key), "%d:%d:%u", floor, index, raw);
    return start_key(events, key);
}

bool events_trigger_floor(EventRuntime *events, int floor)
{
    char key[32];
    snprintf(key, sizeof(key), "%d:floor", floor);
    return start_key(events, key);
}

bool events_active(const EventRuntime *events)
{
    return events && (events->active || event_engine_active(&events->engine));
}

void events_cancel(EventRuntime *events)
{
    if (!events)
        return;
    if (events->L && events->thread_ref != LUA_NOREF)
        luaL_unref(events->L, LUA_REGISTRYINDEX, events->thread_ref);
    events->thread_ref = LUA_NOREF;
    events->active = false;
    events->wait = EVENT_WAIT_NONE;
    clear_actors(events);
    event_engine_cancel(&events->engine);
}

static void engine_update(EventRuntime *events, uint64_t now)
{
    if (events->active)
        return;
    if (!event_engine_active(&events->engine)) {
        /* Timeline and its transitional sub-event are both done. */
        event_engine_clear_actors(&events->engine);
        return;
    }
    if (!event_engine_update(&events->engine, now))
        return;
    char key[EVENT_ENGINE_KEY_LEN];
    if (event_engine_take_continuation(&events->engine, key, sizeof(key))) {
        /* A sub-event that is not ported as a builder yet: run its
         * transitional coroutine, the timeline stays paused meanwhile. */
        if (!start_key(events, key)) {
            char msg[128];
            snprintf(msg, sizeof(msg), "event %s is not implemented", key);
            runtime_log(msg);
        }
    }
    if (events->app)
        state_compute_checksums(&events->app->game.state);
}

void events_update(EventRuntime *events, uint64_t now)
{
    if (!events)
        return;
    engine_update(events, now);
    if (!events->active ||
        (events->wait != EVENT_WAIT_TICKS &&
         events->wait != EVENT_WAIT_CUTSCENE))
        return;
    if (now >= events->wait_until)
        event_resume(events, 0);
}

bool events_handle_key(EventRuntime *events, int key, bool repeat)
{
    if (!events)
        return false;
    if (!events->active) {
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
    if (events->wait == EVENT_WAIT_MESSAGE) {
        if (!repeat && (key == SDLK_RETURN || key == SDLK_KP_ENTER ||
                        key == SDLK_ESCAPE || key == SDLK_SPACE))
            event_resume(events, 0);
        return true;
    }
    if (events->wait == EVENT_WAIT_CUTSCENE) {
        if (!repeat && (key == SDLK_RETURN || key == SDLK_KP_ENTER ||
                        key == SDLK_ESCAPE || key == SDLK_SPACE))
            event_resume(events, 0);
        return true;
    }
    if (events->wait == EVENT_WAIT_CHOICE) {
        if (key == SDLK_UP) {
            events->choice_selected--;
            if (events->choice_selected < 0)
                events->choice_selected = events->choice_count - 1;
        } else if (key == SDLK_DOWN) {
            events->choice_selected++;
            if (events->choice_selected >= events->choice_count)
                events->choice_selected = 0;
        } else if (!repeat && (key == SDLK_RETURN || key == SDLK_KP_ENTER)) {
            lua_State *co = event_thread(events);
            if (co) {
                lua_pushinteger(co, events->choice_selected + 1);
                event_resume(events, 1);
            }
        } else if (!repeat && key == SDLK_ESCAPE) {
            lua_State *co = event_thread(events);
            if (co) {
                lua_pushinteger(co, 0);
                event_resume(events, 1);
            }
        }
        return true;
    }
    return true;
}

static void open_event_libraries(lua_State *L)
{
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    lua_pop(L, 1);
    /* No filesystem or dynamic-code loading from event scripts. */
    lua_pushnil(L); lua_setglobal(L, "dofile");
    lua_pushnil(L); lua_setglobal(L, "loadfile");
    lua_pushnil(L); lua_setglobal(L, "load");
}

bool events_runtime_init(EventRuntime *events, struct App *app,
                         const char *script_path,
                         EventsLogFn log_fn, void *user,
                         char *err, size_t errsz)
{
    if (!events || !script_path || !*script_path) {
        if (err && errsz) snprintf(err, errsz, "missing event script");
        return false;
    }
    memset(events, 0, sizeof(*events));
    events->thread_ref = LUA_NOREF;
    events->app = app;
    events->log_fn = log_fn;
    events->log_user = user;
    lua_State *L = luaL_newstate();
    if (!L) {
        if (err && errsz) snprintf(err, errsz, "cannot create event Lua state");
        return false;
    }
    open_event_libraries(L);
    events->L = L;
    g_rt = events;
    register_api(L);
    int rc = luaL_loadfile(L, script_path);
    if (rc == LUA_OK)
        rc = lua_pcall(L, 0, 0, 0);
    if (rc != LUA_OK) {
        const char *message = lua_tostring(L, -1);
        if (err && errsz)
            snprintf(err, errsz, "Lua error in %s: %s", script_path,
                     message ? message : "unknown error");
        lua_close(L);
        events->L = NULL;
        g_rt = NULL;
        return false;
    }

    /* builders.lua lives next to events.lua. */
    char builders[1024];
    const char *slash = strrchr(script_path, '/');
    size_t dir_len = slash ? (size_t)(slash - script_path + 1) : 0;
    if (dir_len + sizeof("builders.lua") <= sizeof(builders)) {
        memcpy(builders, script_path, dir_len);
        memcpy(builders + dir_len, "builders.lua", sizeof("builders.lua"));
    } else {
        builders[0] = '\0';
    }
    events->engine.log_fn = log_fn;
    events->engine.log_user = user;
    char engine_err[512] = "";
    if (!event_engine_init(&events->engine, builders, engine_err,
                           sizeof(engine_err))) {
        char msg[640];
        snprintf(msg, sizeof(msg), "event builders unavailable: %s",
                 engine_err);
        runtime_log(msg);
    }
    if (app) {
        event_engine_bind(&events->engine, &app->game.state, &app->game.map);
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
            runtime_log(msg);
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
    if (events->L)
        lua_close(events->L);
    events->L = NULL;
    if (g_rt == events)
        g_rt = NULL;
}
