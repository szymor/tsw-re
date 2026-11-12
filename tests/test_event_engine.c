/* Event engine: routing, Timer2-style ticking, continuation hand-over. */
#include "event_engine.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

static StateBlock state;
static MapBlock map;

static void reset_world(void)
{
    memset(&state, 0, sizeof(state));
    memset(&map, 0, sizeof(map));
    map.bytes[1] = 0x01;
    memset(map.bytes + map_floor_offset(3), 0x06, MAP_TILES);
    map.bytes[map_floor_offset(3) + 92] = 0x07;
    state.raw[ST_LIFE] = 1000;
    state.raw[ST_ATTACK] = 100;
    state.raw[ST_DEFENSE] = 100;
    state.raw[ST_FLOOR] = 3;
    state.raw[ST_FLOOR_LIMIT] = 3;
    state.raw[ST_X] = 4;
    state.raw[ST_Y] = 9;
    state.raw[ST_FACING] = 4;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: test_event_engine <builders.lua>\n");
        return 2;
    }
    static EventEngine e;
    char err[512];
    CHECK(event_engine_init(&e, argv[1], err, sizeof(err)));

    CHECK(event_engine_route(3, 92, 0x07) != NULL);
    CHECK(event_engine_route(5, 118, 0x09) == NULL);
    CHECK(event_engine_route(4, 92, 0x07) == NULL);

    reset_world();
    event_engine_bind(&e, &state, &map);
    CHECK(event_engine_trigger(&e, 3, 92, 0x15, 1) == EVENT_ENGINE_NOT_ROUTED);
    CHECK(event_engine_trigger(&e, 3, 92, 0x07, 1) == EVENT_ENGINE_STARTED);
    CHECK(event_engine_active(&e));
    CHECK(state.raw[ST_Y] == 8);
    CHECK(map_tile(&map, 3, 92) == 0x06);
    CHECK(e.vm.count == 41);
    /* A second trigger while active is refused. */
    CHECK(event_engine_trigger(&e, 3, 92, 0x07, 1) == EVENT_ENGINE_FAILED);

    /* Ticking: nothing on the first update (arms the timer), then two
     * commands per 250 ms period. */
    uint64_t t = 1000;
    CHECK(!event_engine_update(&e, t));
    CHECK(e.vm.count == 41);
    CHECK(!event_engine_update(&e, t + 249));
    CHECK(e.vm.count == 41);
    CHECK(!event_engine_update(&e, t + 250));
    CHECK(e.vm.count == 39);
    /* Tail first: sound 7 and a wait, then the first Zeno frame (0xa6 at
     * actor index 0x46) on the next tick. */
    CHECK(!e.actor.active);
    CHECK(!event_engine_update(&e, t + 500));
    CHECK(e.vm.count == 37);
    CHECK(e.actor.active && e.actor.map_index == 0x46 && e.actor.atlas_sprite == 0xa6);

    /* Run on: the last command is op 0x0a (sub-event 20) which pauses the
     * timeline and hands over to the transitional coroutine. */
    bool handed = false;
    for (int i = 0; i < 64 && !handed; i++) {
        t += 250;
        if (i == 0) t += 250;
        handed = event_engine_update(&e, t);
    }
    CHECK(handed);
    CHECK(e.actor.atlas_sprite == 0xb6);
    char key[EVENT_ENGINE_KEY_LEN];
    CHECK(event_engine_take_continuation(&e, key, sizeof(key)));
    CHECK(strcmp(key, "subevent:20") == 0);
    CHECK(!event_engine_active(&e));
    CHECK(!event_engine_take_continuation(&e, key, sizeof(key)));

    /* Revisit: plain walk through the builder is not routed (tile is 0x06). */
    state.raw[ST_Y] = 9;
    CHECK(event_engine_trigger(&e, 3, 92, map_tile(&map, 3, 92), 1)
          == EVENT_ENGINE_NOT_ROUTED);

    /* Fail closed (magician next to the trigger): nothing changes. */
    reset_world();
    map.bytes[map_floor_offset(3) + 91] = 0x97;
    CHECK(event_engine_trigger(&e, 3, 92, 0x07, 1) == EVENT_ENGINE_FAILED);
    CHECK(state.raw[ST_Y] == 9);
    CHECK(map_tile(&map, 3, 92) == 0x07);
    CHECK(!event_engine_active(&e));

    /* Cancel mid-timeline clears everything. */
    reset_world();
    CHECK(event_engine_trigger(&e, 3, 92, 0x07, 1) == EVENT_ENGINE_STARTED);
    event_engine_update(&e, 0);
    event_engine_update(&e, 250);
    event_engine_cancel(&e);
    CHECK(!event_engine_active(&e));
    CHECK(!e.actor.active);
    CHECK(e.aux.v[EVAUX_EVENT_SPRITE] == 0);

    /* Door interval is sticky inside the VM model. */
    e.vm.speed = EVM_SPEED_LOW;
    event_vm_append(&e.vm, EVOP_DOOR_OPEN, 10, 1);
    event_vm_run(&e.vm, 4);
    CHECK(e.vm.interval_ms == 12);

    event_engine_shutdown(&e);
    if (failures) {
        fprintf(stderr, "test_event_engine: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_event_engine: ok\n");
    return 0;
}
