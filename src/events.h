#ifndef TSW_EVENTS_H
#define TSW_EVENTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "event_engine.h"

struct App;

typedef void (*EventsLogFn)(void *user, const char *message);

typedef struct EventRuntime {
    struct App *app;
    EventsLogFn log_fn;
    void *log_user;
    /* Original-handler builders + command VM (event_engine.h): every event
     * runs here. */
    EventEngine engine;
} EventRuntime;

bool events_file_exists(const char *path);

/* `builders_path` is src/events/builders.lua (or its installed copy). */
bool events_runtime_init(EventRuntime *events, struct App *app,
                         const char *builders_path,
                         EventsLogFn log_fn, void *user,
                         char *err, size_t errsz);
void events_runtime_shutdown(EventRuntime *events);
void events_cancel(EventRuntime *events);
/* True while a command timeline or one of its windows is open. */
bool events_active(const EventRuntime *events);

bool events_trigger(EventRuntime *events, int floor, int index, uint8_t raw);
/* A battle was won: run the original's moncheck for the floor. */
bool events_after_battle(EventRuntime *events);
void events_update(EventRuntime *events, uint64_t now);
bool events_handle_key(EventRuntime *events, int key, bool repeat);

#endif
