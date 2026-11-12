#ifndef TSW_EVENTS_H
#define TSW_EVENTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "event_engine.h"

struct lua_State;
struct App;

typedef void (*EventsLogFn)(void *user, const char *message);

#define EVENT_TEXT_LEN 512
#define EVENT_OPTION_LEN 128
#define EVENT_MAX_OPTIONS 4
#define EVENT_MAX_ACTORS 8

typedef enum {
    EVENT_WAIT_NONE = 0,
    EVENT_WAIT_TICKS,
    EVENT_WAIT_MESSAGE,
    EVENT_WAIT_CHOICE,
    EVENT_WAIT_CUTSCENE
} EventWaitKind;

typedef struct EventActor {
    int sprite;
    int x;
    int y;
    bool active;
} EventActor;

typedef struct EventRuntime {
    struct lua_State *L;
    struct App *app;
    int thread_ref;
    bool active;
    EventWaitKind wait;
    uint64_t wait_until;
    char message[EVENT_TEXT_LEN];
    char choice_prompt[EVENT_TEXT_LEN];
    char choice_options[EVENT_MAX_OPTIONS][EVENT_OPTION_LEN];
    int choice_count;
    int choice_selected;
    char cutscene_text[EVENT_TEXT_LEN];
    int cutscene_image;          /* 0=none, 1=open0, 2=open1, 3=open2 */
    EventActor actors[EVENT_MAX_ACTORS];
    EventsLogFn log_fn;
    void *log_user;
    /* Original-handler builders + command VM (event_engine.h).  Routed
     * triggers run here; the coroutine runtime above is transitional and
     * only serves placeholder events and not-yet-ported sub-events. */
    EventEngine engine;
} EventRuntime;

bool events_file_exists(const char *path);

/* Low-level smoke-test loader used by tests/test_lua.c. */
bool events_init(struct lua_State **out, const char *script_path,
                 EventsLogFn log_fn, void *user,
                 char *err, size_t errsz);
void events_shutdown(struct lua_State *lua);

bool events_runtime_init(EventRuntime *events, struct App *app,
                         const char *script_path,
                         EventsLogFn log_fn, void *user,
                         char *err, size_t errsz);
void events_runtime_shutdown(EventRuntime *events);
void events_cancel(EventRuntime *events);
/* True while a coroutine event or a command timeline is running. */
bool events_active(const EventRuntime *events);

bool events_trigger(EventRuntime *events, int floor, int index, uint8_t raw);
bool events_trigger_floor(EventRuntime *events, int floor);
/* A battle was won: run the original's moncheck for the floor. */
bool events_after_battle(EventRuntime *events);
void events_update(EventRuntime *events, uint64_t now);
bool events_handle_key(EventRuntime *events, int key, bool repeat);

#endif
