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
    int seen_resets;              /* engine.resets already handled */
    /* The ranking form's InputQuery being typed (UTF-8). */
    char name_input[RANKING_NAME_LEN];
    bool text_input_on;
    uint64_t next_timer1;         /* next Timer1 tick (500 ms) */
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
void events_update(EventRuntime *events, uint64_t now);
bool events_handle_key(EventRuntime *events, int key, bool repeat);
/* Text typed while the ranking form asks for a name. */
void events_handle_text(EventRuntime *events, const char *text);
/* Item overlay Enter on special-item slot (EventEngineResult). */
int events_use_item(EventRuntime *events, int slot);

#endif
