/* Event engine: routing, Timer2-style ticking, unported-sub-event accounting. */
#include "event_engine.h"

#include <stdio.h>
#include <stdlib.h>
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

static char logged[16][256];
static int nlogged;
static void capture_log(void *user, const char *message)
{
    (void)user;
    if (nlogged < 16)
        snprintf(logged[nlogged++], sizeof(logged[0]), "%s", message);
}

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
        /* Speaker column on some lines, the old two-column form on 86. */
        fputs("9\tzeno\tZeno says hello.\n"
              "86\tI have done what I can. Good luck!\n"
              "87\told_man\tI will raise your power 3 percent.\n"
              "88\told_man\tI will grant you the orb of the hero.\n"
              "202\taltar\tIf you offer me\n"
              "203\taltar\tGold, I will grant your request. Will you?\n"
              "220\tfairy\tJudging by your power, you have\n"
              "221\tfairy\t points.\n"
              "222\tfairy\tReconsider.\n"
              "234\tfairy\t.\n"
              "235\tfairy\tPlay on.\n"
              "247\tmerchant\tA blue key for 50 gold?\n"
              "260\t\tLoaded the Game.\n"
              "134\tStand-in for the stairs text.\n"
              "94\tSlime\n"
              "128\tA key.\n"
              "138\tThe orb.\n"
              "199\tYou beat the\n"
              "200\tYou got\n"
              "201\t gold.\n"
              "218\tWhat?\n"
              "219\tHey!\n", mf);
        fclose(mf);
    }
    CHECK(event_engine_load_messages(&e, "test_event_engine_messages.tsv",
                                     err, sizeof(err)));
    /* Button1's caption starts as the form's "OK". */
    CHECK(strcmp(event_engine_button_caption(&e), "OK") == 0);

    CHECK(event_engine_route(3, 92, 0x07) != NULL);
    CHECK(event_engine_route(5, 118, 0x09) != NULL);
    CHECK(event_engine_route(4, 92, 0x08) != NULL);    /* every raw */
    CHECK(event_engine_route(15, 60, 0x07) != NULL);
    CHECK(event_engine_route(25, 60, 0x07) != NULL);
    CHECK(event_engine_route(30, 60, 0x07) != NULL);
    CHECK(event_engine_route(38, 60, 0x07) != NULL);
    CHECK(event_engine_route(48, 60, 0x07) != NULL);

    reset_world();
    event_engine_bind(&e, &state, &map);
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
    CHECK(e.aux.v[EVAUX_SUBEVENT_ID] == 20);
    CHECK((e.aux.v[EVAUX_UI_SELECTOR] & 0xffff) == 10);
    CHECK(event_engine_dialog(&e) != NULL);
    CHECK(strstr(event_engine_dialog(&e), "Zeno") != NULL);
    CHECK(event_engine_dialog_speaker(&e) == speaker_find("zeno"));
    CHECK(strcmp(event_engine_speaker_name(&e, speaker_find("zeno")),
                 "The Magic Sergeant, Zeno") == 0);
    /* mevent's floor-3 case set Button1.Caption := ListBox2[0xda]. */
    CHECK(strcmp(event_engine_button_caption(&e), "What?") == 0);
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
        char captions[16][16];
        /* openning9 phase 0: open1 dissolves in over the bare panels
         * through masks 0xa7..0xb6, in order, then phase 1 shows it plain. */
        int dis_first = 0, dis_last = 0, dis_seen = 0;
        bool dis_order = true, dis_open1 = true, dis_after = false;
        for (int i = 0; i < 4000 && (event_engine_active(&e) || e.dialog_open);
             i++) {
            t += 400;
            event_engine_update(&e, t);
            if (e.cutscene.dissolve) {
                if (!dis_first) dis_first = e.cutscene.dissolve;
                if (e.cutscene.dissolve < dis_last) dis_order = false;
                if (e.cutscene.dissolve != dis_last) dis_seen++;
                dis_last = e.cutscene.dissolve;
                if (e.cutscene.image != 2) dis_open1 = false;
            } else if (dis_last && e.cutscene.overlay == 3) {
                dis_after = true;
            }
            if (e.dialog_open) {
                if (nd < 16) {
                    snprintf(captions[nd], sizeof(captions[nd]), "%s",
                             event_engine_button_caption(&e));
                    dialogues[nd++] = e.dialog_id;
                }
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
            /* openning2 set "Hey!" (ListBox2[0xdb]) for the capture;
             * openning31 put the form's "OK" back for the wake-up. */
            CHECK(strcmp(captions[0], "Hey!") == 0);
            for (int k = 1; k < 5; k++)
                CHECK(strcmp(captions[k], "OK") == 0);
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
        CHECK(dis_first >= 0xa7 && dis_first <= 0xa8);
        CHECK(dis_last == 0xb6);
        CHECK(dis_order && dis_open1 && dis_after);
        CHECK(dis_seen >= 8);
        CHECK(e.screen_captures == 0);     /* the backdrop: bare panels */
    }

    /* openning9 frame -> mask (ImageList frame - 0x42, 1-based + 1). */
    CHECK(event_engine_dissolve_mask(0xe8) == 0xa7);
    CHECK(event_engine_dissolve_mask(0xf7) == 0xb6);
    /* Sub-dispatch 0xf captures the screen into 0x48c528; the phase-2
     * frames (the floor-50 R-run ending) dissolve open0 over it; a reset
     * (syokidata2) brings the bare panels back. */
    {
        EventEngine *pe = &e;
        event_engine_cancel(pe);
        event_vm_clear(&pe->vm);
        event_vm_append(&pe->vm, EVOP_NOP, 2, 0);
        event_vm_append(&pe->vm, EVOP_SUBDISPATCH, 0xc, 0xe8);
        event_vm_append(&pe->vm, EVOP_SUBDISPATCH, 0xf, 0);
        event_vm_run(&pe->vm, 1);
        CHECK(pe->screen_captures == 1);
        event_vm_run(&pe->vm, 1);
        CHECK(pe->cutscene.image == 1 && pe->cutscene.dissolve == 0xa7);
        event_engine_cancel(pe);
        CHECK(pe->screen_captures == 0);
    }

    /* Revisit: the cell is floor now, so handan just walks onto it. */
    state.raw[ST_FLOOR] = 3;
    state.raw[ST_X] = 4;
    state.raw[ST_Y] = 9;
    CHECK(event_engine_trigger(&e, 3, 92, map_tile(&map, 3, 92), 1)
          == EVENT_ENGINE_STARTED);
    CHECK(state.raw[ST_Y] == 8 && !event_engine_active(&e));

    /* A magician next to the trigger strikes during idou. */
    reset_world();
    map.bytes[map_floor_offset(3) + 91] = 0x97;
    CHECK(event_engine_trigger(&e, 3, 92, 0x07, 1) == EVENT_ENGINE_STARTED);
    CHECK(state.raw[ST_Y] == 8);
    event_engine_cancel(&e);

    /* An ordinary step through handan: idou's checks.  A 0x97 magician
     * above the destination takes 200 life; between two Magic Sergeants
     * the life is halved. */
    reset_world();
    state.raw[ST_X] = 5;
    state.raw[ST_Y] = 6;
    CHECK(event_engine_trigger(&e, 3, 60, 0x06, 1) == EVENT_ENGINE_STARTED);
    CHECK(state.raw[ST_Y] == 5 && !event_engine_active(&e));
    state.raw[ST_Y] = 6;
    map.bytes[map_floor_offset(3) + 49] = 0x97;
    CHECK(event_engine_trigger(&e, 3, 60, 0x06, 1) == EVENT_ENGINE_STARTED);
    CHECK(event_engine_active(&e));
    {
        uint64_t t = 0;
        for (int i = 0; i < 400 && event_engine_active(&e); i++)
            event_engine_update(&e, t += 50);
    }
    CHECK(state.raw[ST_LIFE] == 800);
    state.raw[ST_Y] = 6;
    map.bytes[map_floor_offset(3) + 49] = 0x06;
    map.bytes[map_floor_offset(3) + 59] = 0x5d;
    map.bytes[map_floor_offset(3) + 61] = 0x5d;
    CHECK(event_engine_trigger(&e, 3, 60, 0x06, 1) == EVENT_ENGINE_STARTED);
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
    CHECK(e.aux.v[EVAUX_WORK_558] == 0);

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
    CHECK(event_engine_dialog_speaker(&e) == speaker_find("old_man"));
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
    CHECK(event_engine_dialog_speaker(&e) == -1);     /* no column */
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
    CHECK(e.aux.v[EVAUX_WORK_558] == 1);
    CHECK(e.aux.v[EVAUX_SPRITE] == 1);
    CHECK(state.raw[0x0c] == 0);              /* winning branch */
    /* monshyouji filled the right panel: Red Slime (index 1), picture
     * 0x3e, name ListBox2[0x5f], its stats; the hit's op 0x05 writes the
     * remaining life (0) into Memo5. */
    CHECK(e.monster_panel.index == 1);
    CHECK(e.monster_panel.life == 45 && e.monster_panel.attack == 20 &&
          e.monster_panel.defense == 2);
    CHECK(event_engine_monster_picture(1) == 0x3e);
    CHECK(event_engine_monster_name_id(1) == 0x5f);
    event_vm_run(&e.vm, 18);
    CHECK(e.vm.count == 0);
    CHECK(e.monster_panel.index == 1 && e.monster_panel.life == 0);
    /* new game / load (syokidata2): no monster, the captions again */
    event_engine_cancel(&e);
    CHECK(e.monster_panel.index == -1);
    CHECK(event_engine_monster_name_id(-1) == 0x5d);
    CHECK(event_engine_monster_picture(-1) == -1);
    /* the big monsters and the 0x14.. range of monshyouji's table */
    CHECK(event_engine_monster_picture(0x12) == 0x67);
    CHECK(event_engine_monster_picture(0x13) == 0x79);
    CHECK(event_engine_monster_picture(0x14) == 0x84);
    CHECK(event_engine_monster_picture(0x20) == 0x9c);
    CHECK(event_engine_monster_picture(0x21) == -1);

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
    state.raw[0x9d] = -4;               /* save-format magic */
    remove("option1.dat");
    remove("option11.dat");
    snprintf(e.ranking_dir, sizeof(e.ranking_dir), ".");
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
        /* Ranked (temporary ranking): the name prompt comes first, then
         * the row goes in and option1.dat/option11.dat are written. */
        int rank = state.raw[0x56];
        CHECK(rank >= 1 && rank <= 10 && e.ranking_name_entry);
        CHECK(!event_engine_close_ranking(&e));
        CHECK(event_engine_ranking_name(&e, "Hero", true));
        CHECK(e.ranking.v[(rank - 1) * 6] == state.raw[0x55]);
        CHECK(strcmp(e.ranking.names[rank - 1], "Hero") == 0);
        CHECK(e.ranking_checkpoint_pending);
        Ranking back;
        CHECK(ranking_load(&back, ".", 1, state.raw[0x9d]) == RANKING_OK);
        CHECK(back.v[(rank - 1) * 6] == state.raw[0x55]);
        CHECK(strcmp(back.names[rank - 1], "Hero") == 0);
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

    /* op 0x0e (0x453a36), the octopus' and the dragon's last blow, draws
     * the same kasanegaki composite as op 0x09 (background and sprite from
     * the previous command, mask arg2) before its damage; a player redraw
     * (0x0b/9) drops it like any op-0x09 actor. */
    event_engine_cancel(&e);
    reset_world();
    state.raw[ST_X] = 5;
    state.raw[ST_Y] = 3;
    event_vm_clear(&e.vm);
    event_vm_append(&e.vm, EVOP_SUBDISPATCH, 9, 0);
    event_vm_append(&e.vm, EVOP_NOP, 0x68, 0xb8);
    event_vm_append(&e.vm, EVOP_BATTLE_HIT, 3 * MAP_TILE_COLS + 5, 0xb7);
    event_vm_run(&e.vm, 1);
    {
        const EventVmActor *a = event_engine_actor_at(&e, 3, 3 * MAP_TILE_COLS + 5);
        CHECK(a != NULL && a->background == 0x68 && a->sprite == 0xb8 &&
              a->mask == 0xb7);
    }
    event_vm_run(&e.vm, 2);
    CHECK(event_engine_actor_at(&e, 3, 3 * MAP_TILE_COLS + 5) == NULL);
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

    /* ichicheck's floor-20 event sets Button1's caption to ListBox2[0xda]
     * (not the status bar); the next roujin puts the form's "OK" back,
     * and so does a new game / load (cancel). */
    event_engine_cancel(&e);
    reset_world();
    state.raw[ST_FLOOR] = 20;
    e.status_text[0] = '\0';
    CHECK(event_builders_run(&e.builders, "ichicheck", &e.vm, &e.aux));
    CHECK(strcmp(event_engine_button_caption(&e), "What?") == 0);
    CHECK(e.status_text[0] == '\0');
    event_engine_cancel(&e);
    CHECK(strcmp(event_engine_button_caption(&e), "OK") == 0);
    for (int fl = 25; fl <= 49; fl += 24) {   /* floors 25 and 49 */
        event_engine_cancel(&e);
        reset_world();
        state.raw[ST_FLOOR] = fl;
        CHECK(event_builders_run(&e.builders, "ichicheck", &e.vm, &e.aux));
        CHECK(strcmp(event_engine_button_caption(&e), "What?") == 0);
        CHECK(e.status_text[0] == '\0');
    }
    event_engine_cancel(&e);
    reset_world();
    state.raw[ST_FLOOR] = 20;
    CHECK(event_builders_run(&e.builders, "ichicheck", &e.vm, &e.aux));
    state.raw[ST_FLOOR] = 24;                 /* floor 24 sets "OK" */
    CHECK(event_builders_run(&e.builders, "ichicheck", &e.vm, &e.aux));
    CHECK(strcmp(event_engine_button_caption(&e), "OK") == 0);
    event_engine_cancel(&e);
    reset_world();
    state.raw[ST_FLOOR] = 20;
    CHECK(event_builders_run(&e.builders, "ichicheck", &e.vm, &e.aux));
    state.raw[ST_FLOOR] = 2;
    CHECK(event_builders_run(&e.builders, "roujin", &e.vm, &e.aux));
    CHECK(strcmp(event_engine_button_caption(&e), "OK") == 0);
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
        CHECK(e.choice_focus_no);       /* opens on No (no action) */
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

    /* Item use (img*work -> panel -> Button38/37Click). */
    {
        uint64_t now = 100000;
        #define RUN_DOWN() do { \
            for (int k = 0; k < 4000 && e.vm.count > 0 && e.timer2; k++) { \
                now += 20; event_engine_update(&e, now); } } while (0)
        event_engine_cancel(&e);
        reset_world();
        event_engine_bind(&e, &state, &map);
        int p = state.raw[ST_Y] * MAP_TILE_COLS + state.raw[ST_X];
        /* Passive slots have no handler. */
        CHECK(event_engine_use_item(&e, 3) == EVENT_ENGINE_NOT_ROUTED);
        CHECK(event_engine_use_item(&e, 14) == EVENT_ENGINE_NOT_ROUTED);
        /* Elixir: Yes adds attack*10 + defense*5 and uses it up. */
        state.raw[ST_SPECIAL_FIRST + 4] = 1;
        CHECK(event_engine_use_item(&e, 4) == EVENT_ENGINE_STARTED);
        CHECK(e.item_confirm && e.item_confirm_case == 5);
        CHECK(event_engine_active(&e));
        CHECK(event_engine_item_answer(&e, true));
        CHECK(!e.item_confirm);
        CHECK(state.raw[ST_LIFE] == 1000 + 100 * 10 + 100 * 5);
        CHECK(state.raw[ST_SPECIAL_FIRST + 4] == 0);
        RUN_DOWN();
        CHECK(!event_engine_active(&e));
        /* No keeps the item. */
        state.raw[ST_SPECIAL_FIRST + 4] = 1;
        CHECK(event_engine_use_item(&e, 4) == EVENT_ENGINE_STARTED);
        CHECK(event_engine_item_answer(&e, false));
        CHECK(state.raw[ST_SPECIAL_FIRST + 4] == 1);
        CHECK(!event_engine_active(&e));
        /* Snow Crystal: the lava next to the player becomes floor once
         * the timeline has run; the crystal is kept. */
        state.raw[ST_SPECIAL_FIRST + 11] = 1;
        map.bytes[map_floor_offset(3) + p - 11] = 0x0d;
        map.bytes[map_floor_offset(3) + p - 22] = 0x0d;
        CHECK(event_engine_use_item(&e, 11) == EVENT_ENGINE_STARTED);
        CHECK(e.item_confirm_case == 12);
        CHECK(event_engine_item_answer(&e, true));
        RUN_DOWN();
        CHECK(map_tile(&map, 3, p - 11) == 0x06);
        CHECK(map_tile(&map, 3, p - 22) == 0x0d);
        CHECK(state.raw[ST_SPECIAL_FIRST + 11] == 1);
        /* Orb of Flying next to the stairs: up, then land on the floor's
         * up-arrival cell. */
        state.raw[ST_SPECIAL_FIRST + 2] = 1;
        state.raw[ST_FLOOR_LIMIT] = 5;
        map.bytes[map_floor_offset(3) + p - 1] = 0x0b;
        map.bytes[map_stair_up_offset(4)] = 0x2a;
        CHECK(event_engine_use_item(&e, 2) == EVENT_ENGINE_STARTED);
        CHECK(e.fly_open && e.fly_up_enabled && e.fly_down_enabled);
        /* img4work's mhyouji redraws the floor without the hero and sets
         * state[0xc], so Timer1 does not draw him back while choosing. */
        CHECK(e.hero_erased);
        CHECK(state.raw[0x0c] == 1);
        CHECK(event_engine_fly(&e, 1));
        CHECK(state.raw[ST_FLOOR] == 4);
        CHECK(e.hero_erased);
        event_engine_timer1(&e);
        CHECK(e.hero_erased);
        CHECK(event_engine_fly(&e, 0));
        CHECK(!e.fly_open);
        CHECK(!e.hero_erased);          /* landing: yusyahyouji */
        CHECK(state.raw[ST_X] == 0x2a % 11 && state.raw[ST_Y] == 0x2a / 11);
        RUN_DOWN();
        CHECK(!event_engine_active(&e));
        /* Orb of the Hero (value 1): rows in map order, distinct kinds. */
        event_engine_cancel(&e);
        reset_world();
        event_engine_bind(&e, &state, &map);
        state.raw[ST_SPECIAL_FIRST + 0] = 1;
        map.bytes[map_floor_offset(3) + 0] = 0x3f;     /* Red Slime */
        map.bytes[map_floor_offset(3) + 1] = 0x3d;     /* Green Slime */
        map.bytes[map_floor_offset(3) + 2] = 0x3f;
        CHECK(event_engine_use_item(&e, 0) == EVENT_ENGINE_STARTED);
        CHECK(e.hero_orb_pending && e.hero_rows == 2);
        CHECK(e.hero_row_index[0] == 1 && e.hero_row_index[1] == 0);
        CHECK(e.hero_row_damage[0] == 0);              /* defense 100 */
        state.raw[ST_SPECIAL_FIRST + 0] = 2;           /* not yet usable */
        e.hero_orb_pending = false;
        CHECK(event_engine_use_item(&e, 0) == EVENT_ENGINE_STARTED);
        CHECK(!e.hero_orb_pending && e.hero_rows == 0);
        #undef RUN_DOWN
    }

    /* The status bar (disp 0x44cb34): op 0x02 sets g_anim_sprite and
     * shows ListBox2[sprite + 0x7f]; sprite 1 shows the status string. */
    {
        event_engine_cancel(&e);
        reset_world();
        event_engine_bind(&e, &state, &map);
        event_vm_append(&e.vm, EVOP_SET_SPRITE, 7, 0);
        e.timer2 = true;
        uint64_t now = 700000;
        for (int k = 0; k < 10 && e.vm.count > 0; k++) {
            now += 400; event_engine_update(&e, now);
        }
        CHECK(e.aux.v[EVAUX_SPRITE] == 7);
        CHECK(strcmp(e.status_bar, "Stand-in for the stairs text.") == 0);
        snprintf(e.status_text, sizeof(e.status_text), "status string");
        e.aux.v[EVAUX_SPRITE] = 1;
        event_engine_disp(&e);
        CHECK(strcmp(e.status_bar, "status string") == 0);
    }

    /* The console gets a text when a handler chooses it: two keys log
     * twice; talking to the floor-3 old man again repeats nothing. */
    {
        event_engine_cancel(&e);
        reset_world();
        event_engine_bind(&e, &state, &map);
        int p = state.raw[ST_Y] * MAP_TILE_COLS + state.raw[ST_X];
        map.bytes[map_floor_offset(3) + p - 11] = 0x1d;
        map.bytes[map_floor_offset(3) + p - 22] = 0x1d;
        map.bytes[map_floor_offset(3) + p - 33] = 0x19;    /* old man */
        e.log_fn = capture_log;
        nlogged = 0;
        uint64_t now = 850000;
        for (int step = 1; step <= 5; step++) {
            int target = (state.raw[ST_Y] - 1) * MAP_TILE_COLS + state.raw[ST_X];
            event_engine_trigger(&e, 3, target, map_tile(&map, 3, target), 1);
            for (int k = 0; k < 400 && event_engine_active(&e); k++) {
                now += 50; event_engine_update(&e, now);
                if (e.dialog_open) event_engine_confirm(&e);
            }
        }
        int keys = 0, orb = 0;
        for (int k = 0; k < nlogged; k++) {
            if (strcmp(logged[k], "A key.") == 0) keys++;
            if (strcmp(logged[k], "The orb.") == 0) orb++;
        }
        CHECK(keys == 2);
        CHECK(orb == 1);
        e.log_fn = NULL;
    }

    /* A won fight logs taisen's text once: [0xc7] " " name ". " [0xc8]
     * " " gold [0xc9]. */
    {
        event_engine_cancel(&e);
        reset_world();
        event_engine_bind(&e, &state, &map);
        int p = state.raw[ST_Y] * MAP_TILE_COLS + state.raw[ST_X];
        map.bytes[map_floor_offset(3) + p - 11] = 0x3d;      /* Green Slime */
        e.log_fn = capture_log;
        nlogged = 0;
        CHECK(event_engine_trigger(&e, 3, p - 11, 0x3d, 1) == EVENT_ENGINE_STARTED);
        uint64_t now = 800000;
        for (int k = 0; k < 400 && event_engine_active(&e); k++) {
            now += 100; event_engine_update(&e, now);
        }
        int hits = 0;
        for (int k = 0; k < nlogged; k++)
            if (strcmp(logged[k], "You beat the Slime. You got 1 gold.") == 0)
                hits++;
        CHECK(hits == 1);
        e.log_fn = NULL;
    }

    /* The floor-change wipe: TSW.exe's order table at 0x489b20 (a spiral),
     * kaidanwork's cover pass, then the reveal of the new floor. */
    {
        static const int ORDER[121] = {
            0, 11, 22, 33, 44, 55, 66, 77, 88, 99, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 109, 98, 87, 76, 65, 54, 43, 32, 21, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 12, 23, 34, 45, 56, 67, 78, 89, 100, 101, 102, 103, 104, 105, 106, 107, 108, 97, 86, 75, 64, 53, 42, 31, 20, 19, 18, 17, 16, 15, 14, 13, 24, 35, 46, 57, 68, 79, 90, 91, 92, 93, 94, 95, 96, 85, 74, 63, 52, 41, 30, 29, 28, 27, 26, 25, 36, 47, 58, 69, 80, 81, 82, 83, 84, 73, 62, 51, 40, 39, 38, 37, 48, 59, 70, 71, 72, 61, 50, 49, 60
        };
        for (int n = 1; n <= 121; n++)
            CHECK(event_engine_wipe_cell(n) == ORDER[n - 1]);
        CHECK(event_engine_wipe_cell(0) == -1 && event_engine_wipe_cell(122) == -1);
        event_engine_cancel(&e);
        reset_world();
        event_engine_bind(&e, &state, &map);
        int p = state.raw[ST_Y] * MAP_TILE_COLS + state.raw[ST_X];
        map.bytes[map_floor_offset(3) + p - 11] = 0x0b;    /* up stairs */
        state.raw[ST_FLOOR_LIMIT] = 4;
        CHECK(event_engine_trigger(&e, 3, p - 11, 0x0b, 1) == EVENT_ENGINE_STARTED);
        CHECK(state.raw[ST_FLOOR] == 4 && e.wipe.active && e.wipe.old_floor == 3);
        CHECK(e.wipe.old_player == p && e.wipe.old_tiles[p - 11] == 0x0b);
        uint64_t now = 600000;
        int most = 0;
        for (int k = 0; k < 2000 && e.wipe.active; k++) {
            now += 5; event_engine_update(&e, now);
            int covered = 0;
            for (int c = 0; c < 121; c++) covered += e.wipe.cell[c] == 1;
            if (covered > most) most = covered;
        }
        /* Two stackwork calls per tick, two cells each: the reveal starts
         * in the tick that covers the last cells. */
        CHECK(most >= 117);
        CHECK(!e.wipe.active);
        for (int c = 0; c < 121; c++) CHECK(e.wipe.cell[c] == 2);
    }

    /* The ending: openning9 phase 8 waits for the picture's click, then
     * 0x0b/6 sets option A and resets the game. */
    {
        event_engine_cancel(&e);
        reset_world();
        event_engine_bind(&e, &state, &map);
        int resets = e.resets;
        event_vm_append(&e.vm, EVOP_NOP, 0, 0);
        event_vm_append(&e.vm, EVOP_SUBDISPATCH, 6, 0);
        event_vm_append(&e.vm, EVOP_NOP, 8, 0);
        event_vm_append(&e.vm, EVOP_SUBDISPATCH, 12, 0);
        e.timer2 = true;
        uint64_t now = 500000;
        for (int k = 0; k < 20 && !e.ending_wait; k++) {
            now += 400; event_engine_update(&e, now);
        }
        CHECK(e.ending_wait && !e.timer2 && e.vm.count == 2);
        CHECK(event_engine_active(&e));
        CHECK(event_engine_ending_continue(&e));
        CHECK(!e.ending_wait && e.timer2);
        for (int k = 0; k < 20 && e.vm.count > 0; k++) {
            now += 400; event_engine_update(&e, now);
        }
        CHECK(e.resets == resets + 1);
        CHECK(state.raw[ST_OPTION_A] == 0x2b && state.raw[ST_FLOOR] == 1);
        CHECK(!event_engine_ending_continue(&e));
    }

    /* imagework (0x454c10): frame B's offset per map value. */
    CHECK(event_engine_anim_offset(0x06, 0) == 0);
    CHECK(event_engine_anim_offset(0x0d, 0) == 0x93);
    CHECK(event_engine_anim_offset(0x0e, 0) == 0x93);
    CHECK(event_engine_anim_offset(0x10, 0) == 2);
    CHECK(event_engine_anim_offset(0x13, 0) == 1);
    CHECK(event_engine_anim_offset(0x1a, 0) == 1);
    CHECK(event_engine_anim_offset(0x1b, 0) == 1);
    CHECK(event_engine_anim_offset(0x1b, 1) == 0);
    CHECK(event_engine_anim_offset(0x3c, 0) == 0);
    CHECK(event_engine_anim_offset(0x3d, 0) == 1);
    CHECK(event_engine_anim_offset(0x60, 0) == 1);
    CHECK(event_engine_anim_offset(0x61, 0) == 9);
    CHECK(event_engine_anim_offset(0x6a, 0) == 0);
    CHECK(event_engine_anim_offset(0x73, 0) == 9);
    CHECK(event_engine_anim_offset(0x7c, 0) == 0);
    CHECK(event_engine_anim_offset(0x85, 0) == 1);
    CHECK(event_engine_anim_offset(0x9d, 0) == 1);
    CHECK(event_engine_anim_offset(0x9e, 0) == 0);
    CHECK(event_engine_anim_offset(0xb8, 0) == 0);
    /* The hero: ImageList 0xb8/0xba/0xbc + 8 * (facing - 1). */
    CHECK(event_engine_hero_tile(1, EVENT_HERO_WALK_B) == 0xb9);
    CHECK(event_engine_hero_tile(1, EVENT_HERO_WALK_A) == 0xbb);
    CHECK(event_engine_hero_tile(1, EVENT_HERO_STAND) == 0xbd);
    CHECK(event_engine_hero_tile(4, EVENT_HERO_STAND) == 0xd5);

    /* Timer1Timer: each tick shows the buffer 0x48c5d2 names and redraws
     * the hero standing in it; a step (yusyahyouji1) leaves the walking
     * poses in both, so the hero walks for two ticks, then stands. */
    {
        event_engine_cancel(&e);
        reset_world();
        event_engine_bind(&e, &state, &map);
        CHECK(e.timer1 && e.shown_buffer == 0 && e.timer1_next == 0);
        CHECK(e.hero_pose[0] == EVENT_HERO_STAND &&
              e.hero_pose[1] == EVENT_HERO_STAND);
        int p = state.raw[ST_Y] * MAP_TILE_COLS + state.raw[ST_X];
        CHECK(event_engine_trigger(&e, 3, p - 11, 0x06, 1) == EVENT_ENGINE_STARTED);
        CHECK(e.hero_pose[0] == EVENT_HERO_WALK_A &&
              e.hero_pose[1] == EVENT_HERO_WALK_B);
        event_engine_timer1(&e);
        CHECK(e.shown_buffer == 0 && e.hero_pose[0] == EVENT_HERO_STAND &&
              e.hero_pose[1] == EVENT_HERO_WALK_B);
        CHECK(e.hero_shown_pose == EVENT_HERO_WALK_A);
        event_engine_timer1(&e);
        CHECK(e.shown_buffer == 1 && e.hero_pose[1] == EVENT_HERO_STAND);
        CHECK(e.hero_shown_pose == EVENT_HERO_WALK_B);
        event_engine_timer1(&e);
        CHECK(e.shown_buffer == 0 && e.hero_shown_pose == EVENT_HERO_STAND);
        /* Off: no tick at all. */
        e.timer1 = false;
        event_engine_timer1(&e);
        CHECK(e.shown_buffer == 0 && e.timer1_next == 1);

        /* An op-0x09 composite on the hero's cell is in both buffers; each
         * tick's hero redraw takes it out of the buffer just shown, so it
         * shows once more, then the hero is back.  In battle rendering
         * (state[0xc] = 1) the tick redraws nothing. */
        e.timer1 = true;
        int q = state.raw[ST_Y] * MAP_TILE_COLS + state.raw[ST_X];
        event_vm_clear(&e.vm);
        event_vm_append(&e.vm, EVOP_NOP, 0x3f, 0xb8);
        event_vm_append(&e.vm, EVOP_THREE_SPRITE_ANIM, q, 0xb7);
        event_vm_run(&e.vm, 1);
        const EventVmActor *a = event_engine_actor_at(&e, 3, q);
        CHECK(a && a->buffers == 3 && a->on_screen && e.shown_buffer == 0);
        event_engine_timer1(&e);
        a = event_engine_actor_at(&e, 3, q);
        CHECK(a && a->buffers == (1u << (e.shown_buffer ^ 1)) && a->on_screen);
        event_engine_timer1(&e);
        CHECK(event_engine_actor_at(&e, 3, q) == NULL);
        state.raw[0x0c] = 1;
        event_vm_append(&e.vm, EVOP_NOP, 0x3f, 0xb8);
        event_vm_append(&e.vm, EVOP_THREE_SPRITE_ANIM, q, 0xb7);
        event_vm_run(&e.vm, 1);
        event_engine_timer1(&e);
        event_engine_timer1(&e);
        a = event_engine_actor_at(&e, 3, q);
        CHECK(a && a->buffers == 3);
        state.raw[0x0c] = 0;

        /* Sub-dispatch 7 loads the title into the center panel and stops
         * Timer1; openning9 hides it; 0x0b/0xd stops Timer1 too. */
        event_engine_cancel(&e);
        event_vm_append(&e.vm, EVOP_SUBDISPATCH, 7, 0);
        event_vm_run(&e.vm, 1);
        CHECK(e.center_picture == 1 && !e.timer1);
        state.raw[ST_OPTION_A] = 1;
        event_vm_append(&e.vm, EVOP_SUBDISPATCH, 7, 0);
        event_vm_run(&e.vm, 1);
        CHECK(e.center_picture == 2);
        state.raw[ST_OPTION_A] = 0;
        event_vm_append(&e.vm, EVOP_NOP, 0, 0);
        event_vm_append(&e.vm, EVOP_SUBDISPATCH, 0xc, 0xe8);
        event_vm_run(&e.vm, 1);
        CHECK(e.center_picture == 0);
        event_engine_cancel(&e);
        CHECK(e.timer1);
        event_vm_append(&e.vm, EVOP_SUBDISPATCH, 0xd, 0);
        event_vm_run(&e.vm, 1);
        CHECK(!e.timer1);
        event_engine_cancel(&e);
    }

    /* A whole battle (Red Slime, five turns) with Timer1 beside Timer2:
     * the hit composites on the hero's cell blink -- shown, then gone
     * after the hero redraw, then the next hit. */
    {
        event_engine_cancel(&e);
        reset_world();
        state.raw[ST_ATTACK] = 12;
        map.bytes[map_floor_offset(3) + 92] = 0x3f;
        event_engine_bind(&e, &state, &map);
        CHECK(event_engine_trigger(&e, 3, 92, 0x3f, 1) == EVENT_ENGINE_STARTED);
        int cell = state.raw[ST_Y] * MAP_TILE_COLS + state.raw[ST_X];
        CHECK(cell == 92);
        uint64_t now = 2000000, next1 = now;
        int ons = 0, offs = 0;
        bool was_on = false;
        for (int k = 0; k < 4000 && event_engine_active(&e); k++) {
            now += 10;
            event_engine_update(&e, now);
            if (now >= next1) { event_engine_timer1(&e); next1 = now + 250; }
            const EventVmActor *a = event_engine_actor_at(&e, 3, cell);
            bool on = a && a->on_screen;
            if (on && !was_on) ons++;
            if (!on && was_on) offs++;
            was_on = on;
        }
        CHECK(!event_engine_active(&e));
        CHECK(ons >= 4 && offs >= 4);
        CHECK(e.monster_panel.life == 0);
        /* A second Red Slime straight after: the same monster index and the
         * same full HP in aux, yet the VM's operand must restart from it
         * (g_monster_hp is one global; taisen writes it every battle).
         * The panel's life goes down a hit at a time, not straight to 0. */
        map.bytes[map_floor_offset(3) + cell - MAP_TILE_COLS] = 0x3f;
        CHECK(event_engine_trigger(&e, 3, cell - MAP_TILE_COLS, 0x3f, 4) ==
              EVENT_ENGINE_STARTED);
        CHECK(e.monster_panel.life == 45);
        int lives[16], nl = 0;
        for (int k = 0; k < 4000 && event_engine_active(&e); k++) {
            now += 10;
            event_engine_update(&e, now);
            int life = e.monster_panel.life;
            if (nl == 0 || lives[nl - 1] != life) {
                if (nl < 16) lives[nl++] = life;
            }
        }
        CHECK(!event_engine_active(&e));
        CHECK(nl >= 5);                 /* 45, then four hits to 0 */
        if (nl >= 2)
            CHECK(lives[1] > 0 && lives[nl - 1] == 0);
        event_engine_cancel(&e);
    }

    /* The first-round ending's pictures (Zeno's cell on floor 50): sub
     * 0xe hides the values, sub 0xf captures the screen, phase 2 dissolves
     * open0 over it, then phase 6 (open0, End05..End20), phase 4 (End20
     * shrinking), phase 5 (Image21 over End26) and phase 7 ("The End")
     * under phase 8's button. */
    CHECK(event_engine_ending_picture(1) == 0);
    CHECK(event_engine_ending_picture(2) == 5);
    CHECK(event_engine_ending_picture(0x11) == 20);
    CHECK(event_engine_ending_picture(0x12) == -1);
    {
        int rc[4];
        event_engine_ending_shrink(1, rc);
        CHECK(rc[0] == 0 && rc[1] == 0 && rc[2] == 640 && rc[3] == 400);
        event_engine_ending_shrink(100, rc);
        CHECK(rc[0] == 80 && rc[1] == 50 && rc[2] == 560 && rc[3] == 350);
        event_engine_ending_shrink(400, rc);
        CHECK(rc[0] == 320 && rc[1] == 200 && rc[2] == 320 && rc[3] == 200);
    }
    {
        event_engine_cancel(&e);
        reset_world();
        state.raw[ST_FLOOR] = 50;
        state.raw[ST_FLOOR_LIMIT] = 50;
        memset(map.bytes + map_floor_offset(50), 0x06, MAP_TILES);
        map.bytes[map_floor_offset(50) + 92] = 0x5b;      /* Zeno */
        event_engine_bind(&e, &state, &map);
        CHECK(event_engine_trigger(&e, 50, 92, 0x5b, 1) == EVENT_ENGINE_STARTED);
        int order[8], norder = 0, last = -1, pics = 0, last_pic = -1;
        bool hidden_at_capture = false, dissolved = false, shrink_ok = true;
        int last_shrink = 0, captures_seen = 0;
        uint64_t now = 900000;
        for (int k = 0; k < 20000 && !e.ending_wait; k++) {
            now += 20;
            event_engine_update(&e, now);
            if (e.dialog_open)
                CHECK(event_engine_confirm(&e));
            if (e.screen_captures > captures_seen) {
                captures_seen = e.screen_captures;
                hidden_at_capture = e.hud_hidden;
            }
            const EventCutscene *c = &e.cutscene;
            if (c->image == 1 && c->dissolve)
                dissolved = true;
            int ph = c->ending_phase;
            if (ph && ph != last && norder < 8)
                order[norder++] = ph;
            if (ph) last = ph;
            if (ph == 6) {
                int pic = event_engine_ending_picture(c->ending_frame);
                if (pic != last_pic) { pics++; last_pic = pic; }
            }
            if (ph == 4) {
                if (c->ending_frame < last_shrink) shrink_ok = false;
                last_shrink = c->ending_frame;
            }
        }
        CHECK(e.ending_wait);
        CHECK(captures_seen == 1 && hidden_at_capture && dissolved);
        CHECK(norder == 4);
        if (norder == 4)
            CHECK(order[0] == 6 && order[1] == 4 && order[2] == 5 &&
                  order[3] == 7);
        CHECK(pics == 17 && last_pic == 20);
        CHECK(shrink_ok && last_shrink == 400);
        CHECK(e.cutscene.ending_phase == 7);
        /* The click resets the game: bare panels, values shown again. */
        CHECK(event_engine_ending_continue(&e));
        for (int k = 0; k < 20 && e.vm.count > 0; k++) {
            now += 400; event_engine_update(&e, now);
        }
        CHECK(e.cutscene.ending_phase == 0 && !e.hud_hidden &&
              e.screen_captures == 0);
    }

    /* Door interval is sticky inside the VM model. */
    e.vm.speed = EVM_SPEED_LOW;
    event_vm_append(&e.vm, EVOP_DOOR_OPEN, 10, 1);
    event_vm_run(&e.vm, 4);
    CHECK(e.vm.interval_ms == 12);

    /* Sound requests: op 0x01 plays MediaPlayer1..4/6 while state[0x59]
     * is 0 and starts/stops the tune while state[0x5a] is set; op 0x0f
     * stops and restarts the floor's tune; media_stop / bgm presents. */
    {
        EventAudioCmd q[EVENT_ENGINE_AUDIO_QUEUE];
        event_engine_cancel(&e);
        reset_world();
        event_engine_take_audio(&e, q, EVENT_ENGINE_AUDIO_QUEUE);
        state.raw[0x59] = 0;
        state.raw[0x5a] = 0;
        event_vm_append(&e.vm, EVOP_SOUND, 5, 0);   /* BGM off: nothing */
        event_vm_append(&e.vm, EVOP_SOUND, 9, 0);   /* kai.wav */
        event_vm_append(&e.vm, EVOP_SOUND, 3, 0);   /* taisen.wav (first) */
        event_vm_run(&e.vm, 3);
        int n = event_engine_take_audio(&e, q, EVENT_ENGINE_AUDIO_QUEUE);
        CHECK(n == 2);
        if (n == 2) {
            CHECK(q[0].kind == EVENT_AUDIO_WAV && q[0].arg == 3);
            CHECK(q[1].kind == EVENT_AUDIO_WAV && q[1].arg == 9);
        }
        state.raw[0x59] = 1;                        /* WAV off */
        state.raw[0x5a] = 0x11;
        event_vm_append(&e.vm, EVOP_SOUND, 6, 0);   /* stop */
        event_vm_append(&e.vm, EVOP_SOUND, 5, 0);   /* soundplay 0x11 */
        event_vm_append(&e.vm, EVOP_SOUND, 1, 0);   /* silent */
        event_vm_run(&e.vm, 3);
        n = event_engine_take_audio(&e, q, EVENT_ENGINE_AUDIO_QUEUE);
        CHECK(n == 2);
        if (n == 2) {
            CHECK(q[0].kind == EVENT_AUDIO_BGM && q[0].arg == 0x11);
            CHECK(q[1].kind == EVENT_AUDIO_BGM_STOP);
        }
        state.raw[ST_FLOOR] = 3;                    /* op 0x0f: floor 3 */
        event_vm_append(&e.vm, EVOP_STOP_BGM, 0, 0);
        event_vm_run(&e.vm, 1);
        n = event_engine_take_audio(&e, q, EVENT_ENGINE_AUDIO_QUEUE);
        CHECK(n == 2 && q[0].kind == EVENT_AUDIO_BGM_STOP &&
              q[1].kind == EVENT_AUDIO_BGM && q[1].arg == 5);
        CHECK(state.raw[0x5a] == 5);
        /* The app's switch at game over: the floor's tune again. */
        e.bgm_switch = true;
        event_vm_append(&e.vm, EVOP_SUBDISPATCH, 6, 0);
        event_vm_run(&e.vm, 1);
        n = event_engine_take_audio(&e, q, EVENT_ENGINE_AUDIO_QUEUE);
        bool restarted = false;
        for (int i = 0; i < n; i++)
            if (q[i].kind == EVENT_AUDIO_BGM)
                restarted = true;
        CHECK(e.resets > 0 && restarted);
        e.bgm_switch = false;
        event_engine_cancel(&e);
    }

    /* Speakers of windows filled by present(): a merchant's offer, the
     * altar's, the fairy's score, a line with an empty speaker column. */
    {
        EventPresentFn present = e.builders.present_fn;
        void *pu = e.builders.present_user;
        int32_t a[3] = { 247, 0, 0 };
        present(pu, "richedit_text", a, 1);
        a[0] = 1;
        present(pu, "richedit_show", a, 1);
        CHECK(event_engine_dialog_speaker(&e) == speaker_find("merchant"));
        CHECK(strcmp(event_engine_speaker_name(&e,
                         event_engine_dialog_speaker(&e)), "Merchant") == 0);
        a[0] = 20;
        present(pu, "altar_offer", a, 1);
        CHECK(event_engine_dialog_speaker(&e) == speaker_find("altar"));
        CHECK(strstr(event_engine_dialog(&e), "20") != NULL);
        a[0] = 0; a[1] = 10; a[2] = 1;
        present(pu, "fairy_text", a, 3);
        CHECK(event_engine_dialog_speaker(&e) == speaker_find("fairy"));
        a[0] = 260;
        present(pu, "richedit_text", a, 1);
        CHECK(event_engine_dialog_speaker(&e) == -1);
        a[0] = -1;                    /* RichEdit1 emptied: nobody speaks */
        present(pu, "richedit_text", a, 1);
        CHECK(event_engine_dialog_speaker(&e) == -1);
        a[0] = 0;
        present(pu, "richedit_show", a, 1);
        CHECK(event_engine_dialog_speaker(&e) == -1);
    }
    /* messages.tsv's speaker column and speakers.tsv. */
    {
        CHECK(messages_speaker(&e.messages, 9) &&
              strcmp(messages_speaker(&e.messages, 9), "zeno") == 0);
        CHECK(messages_speaker(&e.messages, 86) == NULL);
        CHECK(messages_speaker(&e.messages, 260) == NULL);
        CHECK(strcmp(messages_get(&e.messages, 260), "Loaded the Game.") == 0);
        CHECK(speaker_find("") == -1 && speaker_find("nobody") == -1);
        CHECK(speaker_def(speaker_find("hero"))->frame_a < 0);
        CHECK(speaker_def(speaker_find("altar"))->frame_b == 0x11);
        FILE *sf = fopen("test_event_engine_speakers.tsv", "w");
        CHECK(sf != NULL);
        if (sf) {
            fputs("old_man\tAncient\n", sf);
            fclose(sf);
        }
        CHECK(event_engine_load_speakers(&e,
                                         "test_event_engine_speakers.tsv"));
        CHECK(strcmp(event_engine_speaker_name(&e, speaker_find("old_man")),
                     "Ancient") == 0);
        CHECK(strcmp(event_engine_speaker_name(&e, speaker_find("thief")),
                     "Thief") == 0);
        sf = fopen("test_event_engine_speakers.tsv", "w");
        if (sf) {
            fputs("ghost\tNobody\n", sf);   /* unknown id: malformed */
            fclose(sf);
        }
        CHECK(!event_engine_load_speakers(&e,
                                          "test_event_engine_speakers.tsv"));
        CHECK(!event_engine_load_speakers(&e, "no-such-speakers.tsv"));
        CHECK(strcmp(event_engine_speaker_name(&e, speaker_find("old_man")),
                     "Old man") == 0);
    }

    event_engine_shutdown(&e);
    if (failures) {
        fprintf(stderr, "test_event_engine: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_event_engine: ok\n");
    return 0;
}
