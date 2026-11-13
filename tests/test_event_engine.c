/* Event engine: routing, Timer2-style ticking, unported-sub-event accounting. */
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
    /* dooropen (0x4770e0) frames: kind 1 halves slide 8 px apart; kind 7
     * (the prison wall) slides down 2p; the last frame copies nothing. */
    {
        EventDoorFrame f;
        CHECK(event_engine_door_frame(1, 8, &f));
        CHECK(f.base_tile == 6 && f.src_tile == 1 && f.nblits == 2);
        CHECK(f.blits[0].sx == 8 && f.blits[0].sw == 8 && f.blits[0].dx == 0);
        CHECK(f.blits[1].sx == 16 && f.blits[1].sw == 8 && f.blits[1].dx == 24);
        CHECK(event_engine_door_frame(7, 4, &f));
        CHECK(f.src_tile == 8 && f.nblits == 1 && f.blits[0].dy == 8 &&
              f.blits[0].sh == 24);
        CHECK(event_engine_door_frame(7, 16, &f) && f.nblits == 0);
        CHECK(event_engine_door_frame(5, 16, &f) && f.nblits == 4);
        CHECK(!event_engine_door_frame(0, 1, &f));
        CHECK(!event_engine_door_frame(13, 1, &f));
    }
    if (argc < 2) {
        fprintf(stderr, "usage: test_event_engine <builders.lua>\n");
        return 2;
    }
    static EventEngine e;
    char err[512];
    CHECK(event_engine_init(&e, argv[1], err, sizeof(err)));
    /* Stand-in ListBox2 entries (the real list comes from the user's
     * TSW.exe via tools/extract_messages.py). */
    FILE *mf = fopen("test_event_engine_messages.tsv", "w");
    CHECK(mf != NULL);
    if (mf) {
        fputs("9\tThe Magic Sergeant, Zeno, says hello.\n"
              "86\tI have done what I can. Good luck!\n"
              "87\tI will raise your power 3 percent.\n"
              "88\tI will grant you the orb of the hero.\n", mf);
        fclose(mf);
    }
    CHECK(event_engine_load_messages(&e, "test_event_engine_messages.tsv",
                                     err, sizeof(err)));

    CHECK(event_engine_route(3, 92, 0x07) != NULL);
    CHECK(event_engine_route(5, 118, 0x09) != NULL);
    CHECK(event_engine_route(4, 92, 0x07) == NULL);
    CHECK(event_engine_route(15, 60, 0x07) != NULL);
    CHECK(event_engine_route(25, 60, 0x07) != NULL);
    CHECK(event_engine_route(30, 60, 0x07) != NULL);
    CHECK(event_engine_route(38, 60, 0x07) != NULL);
    CHECK(event_engine_route(48, 60, 0x07) != NULL);

    reset_world();
    event_engine_bind(&e, &state, &map);
    CHECK(event_engine_trigger(&e, 3, 92, 0x1b, 1) == EVENT_ENGINE_NOT_ROUTED);
    CHECK(event_engine_trigger(&e, 3, 92, 0x07, 1) == EVENT_ENGINE_STARTED);
    CHECK(event_engine_active(&e));
    CHECK(state.raw[ST_Y] == 8);
    CHECK(e.player_step_pending);
    e.player_step_pending = false;
    CHECK(!e.player_step_pending);
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
    CHECK(event_engine_actor_at(&e, 3, 0x46) == NULL);
    CHECK(!event_engine_update(&e, t + 500));
    CHECK(e.vm.count == 37);
    /* Zeno (raw 0x5b) over floor (6) through dissolve mask 0xa6. */
    CHECK(event_engine_actor_at(&e, 3, 0x46) != NULL &&
          event_engine_actor_at(&e, 3, 0x46)->mask == 0xa6 &&
          event_engine_actor_at(&e, 3, 0x46)->sprite == 0x5b &&
          event_engine_actor_at(&e, 3, 0x46)->background == 6);

    /* Run on: the last command is op 0x0a (sub-event 20).  It disables
     * Timer2 and opens the zyouout window with ListBox2[9]. */
    bool drained = false;
    for (int i = 0; i < 64 && !drained; i++) {
        t += 250;
        if (i == 0) t += 250;
        drained = event_engine_update(&e, t);
    }
    CHECK(drained);
    CHECK(event_engine_actor_at(&e, 3, 0x46)->mask == 0xb6);
    CHECK(e.vm.count == 0);
    CHECK(!e.timer2);
    CHECK(e.dialog_open && e.dialog_id == 20);
    CHECK(e.aux.v[EVAUX_ROUJIN_AUX] == 20);
    CHECK((e.aux.v[EVAUX_UI_SELECTOR] & 0xffff) == 10);
    CHECK(event_engine_dialog(&e) != NULL);
    CHECK(strstr(event_engine_dialog(&e), "Zeno") != NULL);
    CHECK(event_engine_active(&e));
    CHECK(e.unported == 0);
    /* Confirm: Button1Click marks state[0x64]; case 20 runs openning2. */
    CHECK(event_engine_confirm(&e));
    CHECK(state.raw[0x64] == 1);
    CHECK(!e.dialog_open);
    CHECK(event_engine_dialog(&e) == NULL);
    /* Case 20 ran openning2: 651 commands, Timer2 on. */
    CHECK(e.vm.count == 651);
    CHECK(e.timer2);
    CHECK(e.unported == 0);
    CHECK(!event_engine_confirm(&e));

    /* Play the whole opening, confirming every dialogue, and compare the
     * end state with the P3b execution oracle (16-event-pilot.md 8). */
    {
        int dialogues[16], nd = 0;
        for (int i = 0; i < 4000 && (event_engine_active(&e) || e.dialog_open);
             i++) {
            t += 400;
            event_engine_update(&e, t);
            if (e.dialog_open) {
                if (nd < 16) dialogues[nd++] = e.dialog_id;
                CHECK(event_engine_confirm(&e));
            }
            CHECK(e.unported == 0);
        }
        /* 20 was confirmed above; then 21, 11, 11, 12, 13. */
        CHECK(nd == 5);
        if (nd == 5) {
            CHECK(dialogues[0] == 21 && dialogues[1] == 11 &&
                  dialogues[2] == 11 && dialogues[3] == 12 &&
                  dialogues[4] == 13);
        }
        CHECK(!event_engine_active(&e));
        CHECK(state.raw[ST_LIFE] == 400);
        CHECK(state.raw[ST_ATTACK] == 10 && state.raw[ST_DEFENSE] == 10);
        CHECK(state.raw[ST_FLOOR] == 2 && state.raw[ST_FLOOR_LIMIT] == 2);
        CHECK(state.raw[ST_X] == 2 && state.raw[ST_Y] == 7);
        CHECK(state.raw[0x5b] == 1 && state.raw[0x5c] == 1 &&
              state.raw[0x5d] == 1 && state.raw[0x64] == 1 &&
              state.raw[0x65] == 1);
        CHECK(state.raw[0x0c] == 0);
        CHECK(map_tile(&map, 3, 92) == 0x06);
        CHECK(e.cutscene.image == 0);
    }

    /* Revisit: plain walk through the builder is not routed (tile is 0x06). */
    state.raw[ST_Y] = 9;
    CHECK(event_engine_trigger(&e, 3, 92, map_tile(&map, 3, 92), 1)
          == EVENT_ENGINE_NOT_ROUTED);

    /* A magician next to the trigger strikes during idou. */
    reset_world();
    map.bytes[map_floor_offset(3) + 91] = 0x97;
    CHECK(event_engine_trigger(&e, 3, 92, 0x07, 1) == EVENT_ENGINE_STARTED);
    CHECK(state.raw[ST_Y] == 8);
    event_engine_cancel(&e);

    /* After an ordinary C step: idou's checks.  A 0x97 magician above
     * takes 200 life; nothing nearby queues nothing. */
    reset_world();
    state.raw[ST_X] = 5;
    state.raw[ST_Y] = 5;
    CHECK(event_engine_after_step(&e) == EVENT_ENGINE_STARTED);
    CHECK(!event_engine_active(&e));
    map.bytes[map_floor_offset(3) + 49] = 0x97;
    CHECK(event_engine_after_step(&e) == EVENT_ENGINE_STARTED);
    CHECK(event_engine_active(&e));
    {
        uint64_t t = 0;
        for (int i = 0; i < 400 && event_engine_active(&e); i++)
            event_engine_update(&e, t += 50);
    }
    CHECK(state.raw[ST_LIFE] == 800);
    /* Between two Magic Sergeants: life halved. */
    map.bytes[map_floor_offset(3) + 49] = 0x06;
    map.bytes[map_floor_offset(3) + 59] = 0x5d;
    map.bytes[map_floor_offset(3) + 61] = 0x5d;
    CHECK(event_engine_after_step(&e) == EVENT_ENGINE_STARTED);
    {
        uint64_t t = 100000;
        for (int i = 0; i < 400 && event_engine_active(&e); i++)
            event_engine_update(&e, t += 50);
    }
    CHECK(state.raw[ST_LIFE] == 400);
    event_engine_cancel(&e);

    /* Cancel mid-timeline clears everything. */
    reset_world();
    CHECK(event_engine_trigger(&e, 3, 92, 0x07, 1) == EVENT_ENGINE_STARTED);
    event_engine_update(&e, 0);
    event_engine_update(&e, 250);
    event_engine_cancel(&e);
    CHECK(!event_engine_active(&e));
    CHECK(event_engine_actor_at(&e, 3, 0x46) == NULL);
    CHECK(e.aux.v[EVAUX_EVENT_SPRITE] == 0);

    /* Old man (handan raw 0x19 -> roujin): text ListBox2[0x19 + 0x3e],
     * the six commands run while the window is open (roujin calls
     * timer2on), confirm (Button1Click case 0) closes it. */
    reset_world();
    state.raw[ST_FLOOR] = 2;
    state.raw[ST_X] = 9;
    state.raw[ST_Y] = 6;
    memset(map.bytes + map_floor_offset(2), 0x06, MAP_TILES);
    map.bytes[map_floor_offset(2) + 76] = 0x19;
    CHECK(event_engine_route(2, 76, 0x19) != NULL);
    CHECK(event_engine_trigger(&e, 2, 76, 0x19, 3) == EVENT_ENGINE_STARTED);
    CHECK(state.raw[ST_X] == 9);              /* no step onto the NPC */
    CHECK(state.raw[0x62] == 1 && state.raw[0x42] == 1);
    CHECK(state.raw[ST_ATTACK] == 103 && state.raw[ST_DEFENSE] == 103);
    CHECK(e.vm.count == 6 && e.timer2);
    CHECK(e.dialog_open && e.dialog_id == 0);
    CHECK(strstr(event_engine_dialog(&e), "3 percent") != NULL);
    t = 100000;
    for (int i = 0; i < 8; i++, t += 250)
        event_engine_update(&e, t);
    CHECK(e.vm.count == 0);
    CHECK(e.dialog_open);                     /* still waiting for OK */
    CHECK(event_engine_active(&e));
    CHECK(event_engine_confirm(&e));
    CHECK(event_engine_dialog(&e) == NULL);
    CHECK(!event_engine_active(&e));
    /* Repeat visit: "I have done what I can" (ListBox2[0x56]). */
    CHECK(event_engine_trigger(&e, 2, 76, 0x19, 3) == EVENT_ENGINE_STARTED);
    CHECK(e.vm.count == 0);
    CHECK(strstr(event_engine_dialog(&e), "done what I can") != NULL);
    CHECK(event_engine_confirm(&e));
    /* Floor 3: Orb of the Hero is 2 while the window is open, 1 after OK. */
    reset_world();
    state.raw[ST_X] = 9;
    state.raw[ST_Y] = 3;
    map.bytes[map_floor_offset(3) + 43] = 0x19;
    CHECK(event_engine_trigger(&e, 3, 43, 0x19, 3) == EVENT_ENGINE_STARTED);
    CHECK(state.raw[ST_SPECIAL_FIRST] == 2);
    CHECK(strstr(event_engine_dialog(&e), "orb of the hero") != NULL);
    CHECK(event_engine_confirm(&e));
    CHECK(state.raw[ST_SPECIAL_FIRST] == 1);
    event_engine_cancel(&e);

    /* taisen2 (0xb/10): the splice passes the command's arg2 as the target
     * index and the builder appends the battle timeline. */
    reset_world();
    state.raw[ST_FLOOR] = 40;
    state.raw[ST_X] = 4;
    state.raw[ST_Y] = 4;
    memset(map.bytes + map_floor_offset(40), 0x06, MAP_TILES);
    map.bytes[map_floor_offset(40) + 40] = 0x3f;  /* Red Slime */
    event_vm_clear(&e.vm);
    e.timer2 = true;
    event_vm_append(&e.vm, EVOP_SUBDISPATCH, 10, 40);
    event_vm_run(&e.vm, 1);
    CHECK(e.vm.count == 18);
    CHECK(e.aux.v[EVAUX_TARGET_INDEX] == 40);
    CHECK(e.aux.v[EVAUX_CURRENT_MONSTER] == 1);
    CHECK(e.aux.v[EVAUX_MONSTER_HP] == 45);
    CHECK(e.aux.v[EVAUX_BATTLE_TURNS] == 1);
    CHECK(e.aux.v[EVAUX_EVENT_SPRITE] == 1);
    CHECK(e.aux.v[EVAUX_SPRITE] == 1);
    CHECK(state.raw[0x0c] == 0);              /* winning branch */

    /* Fairy (yousei, floor 10) on difficulty 0: confirming sub-event 26
     * shows the ranking window and leaves Timer2 off; closing the window
     * (form2onclose 0x43a504) restarts Timer2, and the rest of the timeline
     * removes the fairy at index 0x1b. */
    event_engine_cancel(&e);
    reset_world();
    state.raw[ST_FLOOR] = 10;
    state.raw[ST_X] = 5;
    state.raw[ST_Y] = 3;
    memset(map.bytes + map_floor_offset(10), 0x06, MAP_TILES);
    map.bytes[map_floor_offset(10) + 0x1b] = 0x17;
    CHECK(event_engine_trigger(&e, 10, 0x1b, 0x17, 4) == EVENT_ENGINE_STARTED);
    {
        uint64_t ft = 100000;
        for (int i = 0; i < 200 && !e.dialog_open; i++)
            event_engine_update(&e, ft += 50);
        CHECK(e.dialog_open && e.dialog_id == 26);
        CHECK(event_engine_confirm(&e));
        CHECK(state.raw[0x6a] == 1);
        CHECK(e.ranking_open && !e.timer2);
        CHECK(map_tile(&map, 10, 0x1b) == 0x17);
        CHECK(event_engine_close_ranking(&e));
        CHECK(e.timer2);
        for (int i = 0; i < 200 && e.vm.count > 0; i++)
            event_engine_update(&e, ft += 50);
        CHECK(e.vm.count == 0);
        CHECK(map_tile(&map, 10, 0x1b) == 0x06);
        CHECK(!event_engine_active(&e));
    }
    event_engine_cancel(&e);

    /* op 0x09 composites last until the tile is redrawn: the opening's hit
     * on the player's tile (4,8) disappears at the following op 0x06. */
    event_engine_cancel(&e);
    reset_world();
    state.raw[ST_Y] = 8;
    event_vm_clear(&e.vm);
    event_vm_append(&e.vm, EVOP_SET_TILE, 0x5c, 6);
    event_vm_append(&e.vm, EVOP_NOP, 6, 0xd7);
    event_vm_append(&e.vm, EVOP_THREE_SPRITE_ANIM, 0x5c, 0xd8);
    event_vm_run(&e.vm, 1);
    CHECK(event_engine_actor_at(&e, 3, 0x5c) != NULL &&
          event_engine_actor_at(&e, 3, 0x5c)->sprite == 0xd7);
    event_vm_run(&e.vm, 2);
    CHECK(event_engine_actor_at(&e, 3, 0x5c) == NULL);
    /* the silent 0x11 does not redraw, so the composite stays */
    event_vm_append(&e.vm, EVOP_SET_TILE_SILENT, 0x5c, 6);
    event_vm_append(&e.vm, EVOP_NOP, 6, 0xd7);
    event_vm_append(&e.vm, EVOP_THREE_SPRITE_ANIM, 0x5c, 0xd8);
    event_vm_run(&e.vm, 3);
    CHECK(event_engine_actor_at(&e, 3, 0x5c) != NULL);
    event_engine_cancel(&e);

    /* banpaiawork (0x0b/4) draws the vampire block until one of its cells
     * (4..6, 4..6) is redrawn. */
    event_engine_cancel(&e);
    reset_world();
    state.raw[ST_FLOOR] = 20;
    event_vm_clear(&e.vm);
    event_vm_append(&e.vm, EVOP_SET_TILE, 5 * MAP_TILE_COLS + 5, 0x5f);
    event_vm_append(&e.vm, EVOP_SUBDISPATCH, 4, 12);
    event_vm_run(&e.vm, 1);
    CHECK(e.vampire.active && e.vampire.floor == 20 && e.vampire.stage == 12);
    event_vm_run(&e.vm, 1);
    CHECK(!e.vampire.active);
    event_engine_cancel(&e);

    /* Lost event battle: op 0x03 appends the game-over timeline, op
     * 0x0a/0x5d asks "try again?" (no window), Yes runs op 0x16: options
     * cleared, syokidata state and initial map, the timeline emptied. */
    for (int answer = 1; answer >= 0; answer--) {
        static MapBlock initial;
        memset(&initial, 0x2a, sizeof(initial));
        event_engine_cancel(&e);
        reset_world();
        state.raw[ST_LIFE] = 0;
        state.raw[ST_FLOOR] = 40;
        state.raw[ST_OPTION_A] = 2;
        event_engine_set_initial_map(&e, &initial);
        int resets = e.resets;
        event_vm_append(&e.vm, EVOP_ALTAR, 0, 0);
        e.timer2 = true;
        uint64_t t = 100;
        for (int i = 0; i < 200 && !e.death_prompt; i++)
            event_engine_update(&e, t += 50);
        CHECK(e.death_prompt && !e.dialog_open && !e.dialog_visible);
        CHECK(event_engine_active(&e));
        CHECK(state.raw[0x0c] == 1);
        CHECK(event_engine_death_answer(&e, answer == 1));
        for (int i = 0; i < 200 && event_engine_active(&e); i++)
            event_engine_update(&e, t += 50);
        if (answer) {
            CHECK(e.resets == resets + 1);
            CHECK(state.raw[ST_LIFE] == 1000 && state.raw[ST_FLOOR] == 1);
            CHECK(state.raw[ST_OPTION_A] == 0 && state.raw[0x0c] == 0);
            CHECK(memcmp(&map, &initial, sizeof(map)) == 0);
            CHECK(e.vm.count == -1);
        } else {
            CHECK(e.resets == resets && e.load_enabled);
            CHECK(state.raw[ST_LIFE] == 0 && state.raw[ST_FLOOR] == 40);
            CHECK(e.vm.count > 0 && !e.timer2);
        }
    }
    event_engine_set_initial_map(&e, NULL);
    event_engine_cancel(&e);

    /* Timer1Timer: standing on floor 15's cell 60 (the octopus' cell,
     * floor again) counts state[0x98] up to 2; battle rendering blocks. */
    reset_world();
    state.raw[ST_FLOOR] = 15;
    state.raw[ST_X] = 5;
    state.raw[ST_Y] = 5;
    map.bytes[map_floor_offset(15) + 0x3c] = 0x06;
    state.raw[0x0c] = 1;
    event_engine_timer1(&e);
    CHECK(state.raw[0x98] == 0);
    state.raw[0x0c] = 0;
    for (int i = 0; i < 4; i++)
        event_engine_timer1(&e);
    CHECK(state.raw[0x98] == 2 && state.raw[0x99] == 0);
    state.raw[ST_FLOOR] = 35;
    state.raw[ST_X] = 5;
    state.raw[ST_Y] = 6;
    map.bytes[map_floor_offset(35) + 0x47] = 0x77;
    event_engine_timer1(&e);
    CHECK(state.raw[0x99] == 0);
    map.bytes[map_floor_offset(35) + 0x47] = 0x06;
    event_engine_timer1(&e);
    CHECK(state.raw[0x99] == 1);

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
