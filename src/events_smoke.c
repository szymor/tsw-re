#include "events.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <stdio.h>

static EventsLogFn g_log_fn;
static void *g_log_user;

static int l_log_message(lua_State *L)
{
    const char *message = luaL_checkstring(L, 1);
    if (g_log_fn)
        g_log_fn(g_log_user, message);
    else
        printf("%s\n", message);
    return 0;
}

bool events_init(struct lua_State **out, const char *script_path,
                 EventsLogFn log_fn, void *user,
                 char *err, size_t errsz)
{
    if (!out || !script_path || !*script_path) {
        if (err && errsz) snprintf(err, errsz, "missing Lua script path");
        return false;
    }
    *out = NULL;
    lua_State *L = luaL_newstate();
    if (!L) {
        if (err && errsz) snprintf(err, errsz, "cannot create Lua state");
        return false;
    }
    luaL_openlibs(L);
    g_log_fn = log_fn;
    g_log_user = user;
    lua_register(L, "log_message", l_log_message);
    int rc = luaL_loadfile(L, script_path);
    if (rc == LUA_OK)
        rc = lua_pcall(L, 0, 0, 0);
    if (rc != LUA_OK) {
        const char *message = lua_tostring(L, -1);
        if (err && errsz)
            snprintf(err, errsz, "Lua error in %s: %s", script_path,
                     message ? message : "unknown error");
        lua_close(L);
        g_log_fn = NULL;
        g_log_user = NULL;
        return false;
    }
    *out = L;
    return true;
}

void events_shutdown(struct lua_State *lua)
{
    if (lua)
        lua_close(lua);
    g_log_fn = NULL;
    g_log_user = NULL;
}
