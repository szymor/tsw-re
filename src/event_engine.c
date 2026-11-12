#include "event_engine.h"

#include <stdio.h>
#include <string.h>

static void engine_log(EventEngine *e, const char *fmt, const char *a)
{
    if (!e->log_fn)
        return;
    char buf[640];
    snprintf(buf, sizeof(buf), fmt, a ? a : "");
    e->log_fn(e->log_user, buf);
}

static void on_vm_log(EventVm *vm, const EventVmLog *entry, void *user)
{
    (void)vm;
    EventEngine *e = user;
    if (entry->kind == EVLOG_THREE_SPRITE_ANIM) {
        /* op 0x09: frame arg2 drawn at map index arg1. */
        e->actor.active = true;
        e->actor.map_index = entry->a;
        e->actor.atlas_sprite = entry->b;
    }
}

static void on_vm_sub(EventVm *vm, int kind, int sub, int arg2, void *user)
{
    (void)vm;
    (void)arg2;
    EventEngine *e = user;
    /* Sub-events open zyouout dialogue/choice windows (op 0x0a, id
     * arg1 + 10) or splice sub-handlers (op 0x0b).  None of them is ported
     * as a builder yet: hand over to the transitional coroutine keyed by
     * the sub-event, pausing the timeline while it runs (provisional modal
     * contract, design 01 section 7). */
    if (kind == EVOP_START_SUBEVENT)
        snprintf(e->continuation, sizeof(e->continuation), "subevent:%d",
                 sub + 10);
    else
        snprintf(e->continuation, sizeof(e->continuation), "subdispatch:%d",
                 sub);
}

bool event_engine_init(EventEngine *e, const char *builders_path,
                       char *err, size_t errsz)
{
    EventEngineLogFn log_fn = e->log_fn;
    void *log_user = e->log_user;
    memset(e, 0, sizeof(*e));
    e->log_fn = log_fn;
    e->log_user = log_user;
    event_vm_init(&e->vm, NULL, NULL);
    e->vm.observer = on_vm_log;
    e->vm.observer_user = e;
    e->vm.sub_fn = on_vm_sub;
    e->vm.sub_user = e;
    if (!builders_path || !*builders_path) {
        if (err && errsz) snprintf(err, errsz, "no builder script");
        return false;
    }
    e->have_builders = event_builders_init(&e->builders, builders_path,
                                           err, errsz);
    return e->have_builders;
}

void event_engine_shutdown(EventEngine *e)
{
    if (e->have_builders)
        event_builders_shutdown(&e->builders);
    e->have_builders = false;
}

void event_engine_bind(EventEngine *e, StateBlock *state, MapBlock *map)
{
    e->vm.state = state;
    e->vm.map = map;
}

const char *event_engine_route(int floor, int index, uint8_t raw)
{
    (void)index;
    /* handan raw 0x07 -> mevent; only the floor-3 case is ported
     * (16-event-pilot.md, golden vectors handan_floor3*). */
    if (raw == 0x07 && floor == 3)
        return "handan";
    return NULL;
}

EventEngineResult event_engine_trigger(EventEngine *e, int floor, int index,
                                       uint8_t raw, int direction)
{
    const char *builder = event_engine_route(floor, index, raw);
    if (!builder || !e->have_builders || !e->vm.state || !e->vm.map)
        return EVENT_ENGINE_NOT_ROUTED;
    if (event_engine_active(e))
        return EVENT_ENGINE_FAILED;
    e->aux.v[EVAUX_TARGET_INDEX] = index;
    e->aux.v[EVAUX_DIRECTION] = direction;
    e->continuation[0] = '\0';
    if (!event_builders_run(&e->builders, builder, &e->vm, &e->aux)) {
        engine_log(e, "event builder failed: %s", e->builders.err);
        return EVENT_ENGINE_FAILED;
    }
    e->ticking = false;
    return EVENT_ENGINE_STARTED;
}

bool event_engine_active(const EventEngine *e)
{
    return e->vm.count > 0 || e->continuation[0] != '\0';
}

bool event_engine_update(EventEngine *e, uint64_t now)
{
    if (e->continuation[0])
        return true;                  /* paused for a transitional sub-event */
    if (e->vm.count <= 0) {
        e->ticking = false;
        return false;
    }
    if (!e->ticking) {
        e->ticking = true;
        e->next_tick = now + (uint64_t)e->vm.interval_ms;
        return false;
    }
    int guard = 0;
    while (e->vm.count > 0 && now >= e->next_tick && guard++ < 256) {
        event_vm_tick(&e->vm);
        int period = e->vm.interval_ms > 0 ? e->vm.interval_ms : 1;
        e->next_tick += (uint64_t)period;
        if (e->continuation[0])
            return true;
    }
    if (e->vm.count <= 0) {
        e->ticking = false;
        e->actor.active = false;
        return true;
    }
    return false;
}

bool event_engine_take_continuation(EventEngine *e, char *out, size_t outsz)
{
    if (!e->continuation[0]) {
        if (out && outsz) out[0] = '\0';
        return false;
    }
    if (out && outsz)
        snprintf(out, outsz, "%s", e->continuation);
    e->continuation[0] = '\0';
    return true;
}

void event_engine_cancel(EventEngine *e)
{
    event_vm_clear(&e->vm);
    memset(&e->aux, 0, sizeof(e->aux));
    e->aux.v[EVAUX_DIRECTION] = 1;
    e->continuation[0] = '\0';
    e->ticking = false;
    e->actor.active = false;
}
