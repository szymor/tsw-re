#ifndef TSW_EVENTS_H
#define TSW_EVENTS_H

#include <stdbool.h>
#include <stddef.h>

struct lua_State;

typedef void (*EventsLogFn)(void *user, const char *message);

bool events_file_exists(const char *path);

/* Load an event script into a fresh Lua 5.4 state.  The script may call
 * log_message(string), which is forwarded to log_fn.  The caller owns the
 * returned state and must pass it to events_shutdown(). */
bool events_init(struct lua_State **out, const char *script_path,
                 EventsLogFn log_fn, void *user,
                 char *err, size_t errsz);

void events_shutdown(struct lua_State *lua);

#endif
