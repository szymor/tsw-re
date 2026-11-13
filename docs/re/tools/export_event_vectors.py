#!/usr/bin/env python3
"""Export golden event vectors from the native oracle.

Each witness (a handler entry plus a deterministic synthetic snapshot) is run
through the original x86 code in ``native_event_oracle.NativeHarness`` and the
observable result is written as one golden vector.  The engine's Lua builders
are accepted when they reproduce these vectors (``tests/test_event_parity.c``).

Vectors are generated data: they are derived from the user's own TSW.exe and
are written outside the repository (default ``$XDG_CACHE_HOME/tsw/vectors``).
Never commit them.

Two files per witness:

``<case>.json``  full vector (schema below), for analysis tools;
``<case>.vec``   line-oriented text form read by the C parity test.

JSON schema (``11-event-analysis-plan.md`` section 9)::

    schema          "tsw-event-vector/1"
    binary_sha256   pinned hash
    case_id         witness name
    handler         builder name the engine must provide
    entry           native entry address
    snapshot        {state[171], map_b64 (6273 bytes), aux{}, floor}
    builder         {ok, termination, command_words[[op,a1,a2]...],
                     state_diff, map_diff, aux_final, aux_diff}
    drain           strict execution-oracle result (run_execution_oracle:
                    stackwork drain + dialogue confirms): {stop, steps,
                    dialogues[], state_diff, map_diff}; null when the drain
                    stopped or was tainted, or with --no-drain
    executed        sorted executed instruction addresses
    executed_runs   contiguous executed address runs [start, end) in order

``.vec`` format, one record per line (all numbers decimal)::

    case <id>          handler <name>        entry <addr>
    state <i> <v>      (nonzero initial state ints)
    map <hex>          (6273-byte initial map block as hex)
    aux <name> <v>
    cmd <op> <a1> <a2> (builder output in append order)
    dstate <i> <v>     (final value of every changed state int)
    dmap <off> <v>     (final value of every changed map byte)
    daux <name> <v>    (final value of every changed auxiliary)
    drain_dialog <id>  (drain: g_subevent_id of each confirmed window, in order)
    drain_message_dlg <n>  (drain: MessageDlg answer when not 6 = mrYes)
    xstate <i> <v>     (drain: final value of every changed state int)
    xmap <off> <v>     (drain: final value of every changed map byte)
    drain_end          (present only with a complete, untainted drain)
    end

Usage::

    python3 docs/re/tools/export_event_vectors.py            # all witnesses
    python3 docs/re/tools/export_event_vectors.py --list
    python3 docs/re/tools/export_event_vectors.py --case roujin -d /tmp/vec
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import event_snapshot  # noqa: E402
import native_event_oracle as native  # noqa: E402

SCHEMA = "tsw-event-vector/1"
DEFAULT_EXE = Path.home() / (
    ".wine/drive_c/Program Files (x86)/Tower of the Sorcerer/TSW.exe")


def default_out_dir() -> Path:
    base = os.environ.get("XDG_CACHE_HOME") or str(Path.home() / ".cache")
    return Path(base) / "tsw" / "vectors"


@dataclass
class Witness:
    case_id: str
    handler: str          # engine builder name
    entry: int            # native entry (true function entry)
    floor: int
    x: int
    y: int
    flag_index: int = 0x62
    flag_value: int = 0
    tile: tuple[int, int] | None = None
    tiles: list[tuple[int, int]] = field(default_factory=list)  # more tiles
    target_index: int = 0
    sound_state: int = 0
    extra_state: dict[int, int] = field(default_factory=dict)
    aux_overrides: dict[str, int] = field(default_factory=dict)
    native_commands: list[tuple[int, int, int]] = field(default_factory=list)
    native_count: int | None = None
    with_drain: bool = True
    message_dlg: int = 6            # MessageDlg answer: 6 mrYes, 7 mrNo
    note: str = ""

    def snapshot(self) -> event_snapshot.Snapshot:
        # Mirrors snapshot() in test_compose_event_timeline.py so the vectors
        # use exactly the inputs of the static/native parity test.
        state = [0] * event_snapshot.STATE_INT_COUNT
        state[0] = 1000
        state[1] = 100
        state[2] = 100
        state[0x010 // 4] = self.floor
        state[0x018 // 4] = self.x
        state[0x01C // 4] = self.y
        state[0x160 // 4] = 4
        state[0x168 // 4] = self.sound_state
        state[self.flag_index] = self.flag_value
        for index, value in self.extra_state.items():
            state[index] = value
        map_block = bytearray(event_snapshot.MAP_BLOCK)
        for index, value in ([self.tile] if self.tile is not None else []) + list(self.tiles):
            map_block[2 + self.floor * 123 + index] = value
        aux = dict(event_snapshot.AUX_DEFAULTS)
        aux.update({"floor": self.floor, "x": self.x, "y": self.y,
                    "target_index": self.target_index})
        aux.update(self.aux_overrides)
        return event_snapshot.Snapshot(state, bytes(map_block), aux=aux,
                                       source=f"vector:{self.case_id}")


# Pilot witness matrix (16-event-pilot.md section 4).
WITNESSES = [
    Witness("floor10", "floor10", 0x46CB1C, 10, 5, 10, 0x6A,
            note="armed: area-1 flag clear"),
    Witness("floor10_guard", "floor10", 0x46CB1C, 10, 5, 10, 0x6A, 1,
            note="guard: flag 0x6a already set"),
    Witness("floor10_sound", "floor10", 0x46CB1C, 10, 5, 10, 0x6A,
            sound_state=1, note="nonzero sound state"),
    Witness("floor15", "ichicheck", 0x46CB1C, 15, 5, 5,
            note="floor 15: one door + sound"),
    Witness("floor25", "ichicheck", 0x46CB1C, 25, 5, 5,
            note="floor 25: sub-event 0x17 + door"),
    Witness("floor25_sound", "ichicheck", 0x46CB1C, 25, 5, 5,
            sound_state=5, note="floor 25 with BGM state 7"),
    Witness("floor30", "ichicheck", 0x46CB1C, 30, 5, 5,
            note="floor 30: two doors"),
    Witness("floor38", "ichicheck", 0x46CB1C, 38, 5, 5,
            note="floor 38: sprite 0x72 + door kind 8"),
    Witness("floor33", "ichicheck", 0x46CB1C, 33, 5, 5,
            note="floor 33: two-door path"),
    Witness("floor33_pos107", "ichicheck", 0x46CB1C, 33, 8, 9,
            note="floor 33: player at index 0x6b, sprite + door kind 8"),
    Witness("floor43", "ichicheck", 0x46CB1C, 43, 5, 5,
            tile=(8, 0x5d), note="floor 43: tile 8 is 0x5d"),
    Witness("floor43_no", "ichicheck", 0x46CB1C, 43, 5, 5,
            tile=(8, 6), note="floor 43: tile 8 not 0x5d"),
    Witness("floor42", "ichicheck", 0x46CB1C, 42, 5, 5,
            note="floor 42: four sub-events, madoushi/zeno, floor-35 tile"),
    Witness("floor24", "ichicheck", 0x46CB1C, 24, 5, 5,
            with_drain=False,
            note="floor 24: 46 dialogue commands + 173-command animation"),
    Witness("floor24_done", "ichicheck", 0x46CB1C, 24, 5, 5,
            with_drain=False,
            extra_state={0x9c: 1}, note="floor 24: one-shot already set"),
    Witness("floor24_sound", "ichicheck", 0x46CB1C, 24, 5, 5,
            with_drain=False,
            sound_state=5, note="floor 24 with sound"),
    Witness("floor24_carry", "ichicheck", 0x46CB1C, 24, 5, 5,
            tile=(71, 0x3f), extra_state={159: 1}, with_drain=False,
            note="floor 24: option A set, option B clear -> taisen2 branch"),
    Witness("floor40", "ichicheck", 0x46CB1C, 40, 5, 5,
            tile=(71, 0x3f), with_drain=False, note="floor 40: five taisen2 waves, traps, doors"),
    Witness("floor40_traps", "ichicheck", 0x46CB1C, 40, 5, 5,
            tile=(71, 0x3f), with_drain=False,
            tiles=[(i, 6) for i in (37,36,35,39,40,41,14,13,12,18,19,20,16)],
            note="floor 40: all thirteen trap tiles still floor"),
    Witness("floor20", "ichicheck", 0x46CB1C, 20, 5, 5,
            note="floor 20: 32 banpaiawork stages + door"),
    Witness("floor20_sound", "ichicheck", 0x46CB1C, 20, 5, 5,
            sound_state=5,
            note="floor 20 with sound 1/5 and final sound 6"),
    Witness("floor32", "ichicheck", 0x46CB1C, 32, 5, 5,
            tile=(104, 0x3f), with_drain=False,
            note="floor 32: taisen2 then 13 monster moves"),
    Witness("mevent_floor42", "mevent", 0x46397C, 42, 5, 5,
            target_index=60, note="mevent floor 42: ichicheck only"),
    Witness("mevent_floor24", "mevent", 0x46397C, 24, 5, 5,
            target_index=60, with_drain=False, note="mevent floor 24: idou, clear, ichicheck"),
    Witness("mevent_floor40", "mevent", 0x46397C, 40, 5, 5,
            target_index=60, tiles=[(71, 0x3f)], with_drain=False,
            note="mevent floor 40: BGM 0x12, ichicheck"),
    Witness("handan_floor40", "handan", 0x444490, 40, 5, 5,
            tile=(60, 0x07), target_index=60, tiles=[(71, 0x3f)],
            with_drain=False,
            note="floor-40 raw-0x07 trigger -> mevent -> ichicheck"),
    Witness("handan_floor24", "handan", 0x444490, 24, 5, 5,
            tile=(60, 0x07), target_index=60, with_drain=False,
            note="floor-24 raw-0x07 trigger -> mevent -> ichicheck"),
    Witness("handan_floor42", "handan", 0x444490, 42, 5, 5,
            tile=(60, 0x07), target_index=60,
            note="floor-42 raw-0x07 trigger -> mevent -> ichicheck"),
    Witness("mevent_floor20", "mevent", 0x46397C, 20, 5, 7,
            target_index=82, note="mevent floor 20: idou to index 82"),
    Witness("handan_floor20", "handan", 0x444490, 20, 5, 7,
            tile=(82, 0x07), target_index=82,
            note="floor-20 raw-0x07 trigger -> mevent -> ichicheck"),
    Witness("mevent_floor32", "mevent", 0x46397C, 32, 5, 5,
            target_index=60, tiles=[(104, 0x3f)], with_drain=False,
            note="mevent floor 32: ichicheck only"),
    Witness("handan_floor32", "handan", 0x444490, 32, 5, 5,
            tile=(60, 0x07), target_index=60, tiles=[(104, 0x3f)],
            with_drain=False,
            note="floor-32 trigger"),
    Witness("floor49", "ichicheck", 0x46CB1C, 49, 5, 5,
            note="floor 49: Zeno, madoushi2"),
    Witness("floor49_sound", "ichicheck", 0x46CB1C, 49, 5, 5,
            sound_state=5,
            note="floor 49 with sound; madoushi2"),
    Witness("floor48", "ichicheck", 0x46CB1C, 48, 5, 5,
            note="floor 48: tile 0 -> 0x5d + sound 2"),
    Witness("mevent_floor15", "mevent", 0x46397C, 15, 5, 5,
            target_index=60, note="mevent floor 15: idou, clear, ichicheck"),
    Witness("mevent_floor25", "mevent", 0x46397C, 25, 5, 5,
            target_index=60, note="mevent floor 25: idou, clear, BGM, ichicheck"),
    Witness("mevent_floor30", "mevent", 0x46397C, 30, 5, 5,
            target_index=60, note="mevent floor 30: idou, clear, ichicheck"),
    Witness("mevent_floor38", "mevent", 0x46397C, 38, 5, 5,
            target_index=60, note="mevent floor 38: idou, clear, ichicheck"),
    Witness("mevent_floor48", "mevent", 0x46397C, 48, 5, 5,
            target_index=60, note="mevent floor 48: ichicheck only"),
    Witness("mevent_floor33", "mevent", 0x46397C, 33, 5, 5,
            target_index=60, note="mevent floor 33: idou, clear, ichicheck"),
    Witness("mevent_floor43", "mevent", 0x46397C, 43, 5, 5,
            target_index=60, tile=(8, 0x5d),
            note="mevent floor 43: idou, clear, ichicheck"),
    Witness("mevent_floor49", "mevent", 0x46397C, 49, 5, 5,
            target_index=60, sound_state=5,
            note="mevent floor 49: BGM 0x13, ichicheck, madoushi2"),
    Witness("handan_floor15", "handan", 0x444490, 15, 5, 5,
            tile=(60, 0x07), target_index=60,
            note="floor-15 raw-0x07 trigger -> mevent -> ichicheck"),
    Witness("handan_floor25", "handan", 0x444490, 25, 5, 5,
            tile=(60, 0x07), target_index=60,
            note="floor-25 raw-0x07 trigger -> mevent -> ichicheck"),
    Witness("handan_floor30", "handan", 0x444490, 30, 5, 5,
            tile=(60, 0x07), target_index=60,
            note="floor-30 raw-0x07 trigger -> mevent -> ichicheck"),
    Witness("handan_floor38", "handan", 0x444490, 38, 5, 5,
            tile=(60, 0x07), target_index=60,
            note="floor-38 raw-0x07 trigger -> mevent -> ichicheck"),
    Witness("handan_floor48", "handan", 0x444490, 48, 5, 5,
            tile=(60, 0x07), target_index=60,
            note="floor-48 raw-0x07 trigger -> mevent -> ichicheck"),
    Witness("handan_floor33", "handan", 0x444490, 33, 5, 5,
            tile=(60, 0x07), target_index=60,
            note="floor-33 raw-0x07 trigger -> mevent -> ichicheck"),
    Witness("handan_floor43", "handan", 0x444490, 43, 5, 5,
            tile=(60, 0x07), target_index=60, tiles=[(8, 0x5d)],
            note="floor-43 raw-0x07 trigger -> mevent -> ichicheck"),
    Witness("handan_floor49", "handan", 0x444490, 49, 5, 5,
            tile=(60, 0x07), target_index=60, sound_state=5,
            note="floor-49 raw-0x07 trigger -> mevent -> ichicheck, madoushi2"),
    Witness("roujin", "roujin", 0x449708, 2, 2, 7,
            note="floor 2 first visit"),
    Witness("roujin_flag", "roujin", 0x449708, 2, 2, 7, flag_value=1,
            note="floor 2 repeat (flag 0x62 set)"),
    Witness("roujin_selector", "roujin", 0x449708, 2, 2, 7,
            aux_overrides={"roujin_selector": 1},
            note="floor 2 with roujin_selector 1"),
    Witness("roujin_floor3", "roujin", 0x449708, 3, 9, 5),
    Witness("roujin_floor4", "roujin", 0x449708, 4, 9, 5),
    Witness("roujin_floor6", "roujin", 0x449708, 6, 9, 5),
    Witness("roujin_floor3_repeat", "roujin", 0x449708, 3, 9, 5,
            extra_state={0x43: 1}),
    Witness("roujin_floor16", "roujin", 0x449708, 16, 9, 5),
    Witness("roujin_floor16_gate", "roujin", 0x449708, 16, 9, 5,
            extra_state={0x6D: 1}),
    Witness("roujin_floor16_off", "roujin", 0x449708, 16, 8, 5),
    Witness("zenolivework", "zenolivework", 0x461A0C, 3, 5, 10),
    Witness("openning2", "openning2", 0x464048, 1, 5, 10),
    Witness("handan_floor3", "handan", 0x444490, 3, 4, 8,
            tile=(92, 0x07), target_index=92,
            note="floor-3 raw-0x07 trigger (player on target tile)"),
    Witness("handan_floor3_adjacent", "handan", 0x444490, 3, 4, 9,
            tile=(92, 0x07), target_index=92, aux_overrides={"direction": 1},
            note="floor-3 trigger entered from (4,9) moving up"),
    Witness("handan_floor3_cleared", "handan", 0x444490, 3, 4, 8,
            tile=(92, 0x06), target_index=92,
            note="tile already cleared: no event"),
    # handan raw 0x19 -> roujin (0x44538a), player next to the old man.
    Witness("handan_roujin_floor2", "handan", 0x444490, 2, 9, 6,
            tile=(76, 0x19), target_index=76,
            note="floor-2 old man entered from (9,6)"),
    Witness("handan_roujin_floor3", "handan", 0x444490, 3, 9, 3,
            tile=(43, 0x19), target_index=43,
            note="floor-3 old man (Orb of the Hero) entered from (9,3)"),
    # handan raw 0x07 -> mevent floor 10 -> idou, tile cleared, ichicheck.
    Witness("handan_floor10", "handan", 0x444490, 10, 5, 5,
            tile=(49, 0x07), target_index=49,
            note="floor-10 trigger (5,4) entered from (5,5)"),
    Witness("handan_floor10_cleared", "handan", 0x444490, 10, 5, 5,
            tile=(49, 0x07), target_index=49, extra_state={0x6a: 1},
            note="floor-10 trigger after the area is cleared"),
    # yousei (0x478f0c): the fairy (score, rank, save0 checkpoint).
    Witness("yousei_floor10", "yousei", 0x478F0C, 10, 5, 5,
            extra_state={0: 1500, 1: 30, 2: 30, 8: 2, 9: 1, 10: 1},
            note="area 1: score, temporary ranking, sub-event 26"),
    Witness("yousei_floor10_strong", "yousei", 0x478F0C, 10, 5, 5,
            extra_state={0: 3000, 1: 60, 2: 60, 0x0e: 5}, sound_state=5,
            note="area 1, high score, 6th visit, sound on"),
    Witness("yousei_floor20", "yousei", 0x478F0C, 20, 5, 5,
            extra_state={0: 2000, 1: 120, 2: 110},
            note="area 2"),
    Witness("yousei_floor10_hard", "yousei", 0x478F0C, 10, 5, 5,
            extra_state={0: 2500, 1: 40, 2: 40, 159: 1},
            note="area 1, option A = 1 (drain resumes Timer2)"),
    Witness("yousei_floor40", "yousei", 0x478F0C, 40, 5, 5,
            extra_state={0: 9000, 1: 600, 2: 500},
            note="area 4"),
    Witness("button1click_1a_d0", "button1click", 0x44DA10, 10, 5, 10,
            aux_overrides={"roujin_aux": 0x1a},
            note="fairy confirm, difficulty 0: ranking form, Timer2 stays off"),
    Witness("handan_yousei_floor10", "handan", 0x444490, 10, 5, 1,
            tile=(5, 0x17), target_index=5,
            note="handan raw 0x17 -> yousei"),
    # handan case 9 (0x4447df): the secret walls slide down (door kind 7).
    Witness("handan_secret_wall_floor5", "handan", 0x444490, 5, 8, 9,
            tile=(118, 0x09), target_index=118,
            note="floor-5 secret wall (8,10) pushed from (8,9)"),
    Witness("handan_secret_wall_floor9", "handan", 0x444490, 9, 9, 5,
            tile=(53, 0x09), target_index=53, sound_state=5,
            note="floor-9 secret wall (9,4) pushed from (9,5), sound on"),
    # syounin (0x449014): the merchants; Button2Click (0x44e1cc) = Yes,
    # Button7Click (0x44ea90) = No.
    Witness("syounin_floor2", "syounin", 0x449014, 2, 4, 4,
            note="floor-2 gift offer: Yes only"),
    Witness("syounin_floor6", "syounin", 0x449014, 6, 7, 4,
            note="floor-6 blue key offer"),
    Witness("syounin_floor7", "syounin", 0x449014, 7, 5, 1,
            note="floor-7 five keys offer"),
    Witness("syounin_floor6_thanks", "syounin", 0x449014, 6, 7, 4,
            extra_state={0x2b: 1}, note="bought: thank-you text, talk flag 1"),
    Witness("syounin_floor6_thanks_orb", "syounin", 0x449014, 6, 7, 4,
            extra_state={0x2b: 1, 18: 1},
            note="bought, Orb of Wisdom: talk flag 2"),
    Witness("syounin_floor6_again", "syounin", 0x449014, 6, 7, 4,
            extra_state={0x2b: 1, 0x37: 1}, note="talked: zyouout 0x1c"),
    Witness("syounin_floor2_again", "syounin", 0x449014, 2, 4, 4,
            extra_state={0x2a: 1, 0x36: 2}, note="floor 2 talked: zyouout 0x1d"),
    Witness("syounin_floor12_x1", "syounin", 0x449014, 12, 1, 5,
            note="floor-12 red key offer"),
    Witness("syounin_floor12_x9", "syounin", 0x449014, 12, 9, 5,
            note="floor-12 key store (repeatable)"),
    Witness("syounin_floor28", "syounin", 0x449014, 28, 5, 5,
            note="floor-28 key buyer"),
    Witness("syounin_floor47", "syounin", 0x449014, 47, 5, 5,
            note="floor-47 super mattock offer"),
    Witness("syounin_floor4", "syounin", 0x449014, 4, 5, 5,
            note="no merchant on this floor: stale selectors"),
    Witness("handan_syounin_floor6", "handan", 0x444490, 6, 7, 4,
            tile=(40, 0x13), target_index=40,
            note="handan raw 0x13 -> syounin from (7,4)"),
] + [
    Witness(f"button2click_{name}", "button2click", 0x44E1CC, floor, 5, 5,
            extra_state=extra, aux_overrides=aux_o, note=note)
    for name, floor, extra, aux_o, note in [
        ("floor6", 6, {3: 100},
         {"shop_price": 0x32, "shop_flag": 0x2b, "shop_talk_flag": 0x37},
         "buy the blue key"),
        ("floor6_poor", 6, {3: 10},
         {"shop_price": 0x32, "shop_flag": 0x2b, "shop_talk_flag": 0x37},
         "not enough gold"),
        ("floor2", 2, {},
         {"shop_price": 0, "shop_flag": 0x2a, "shop_talk_flag": 0x36},
         "floor-2 gift: 1000 gold"),
        ("floor7", 7, {3: 50},
         {"shop_price": 0x32, "shop_flag": 0x2c, "shop_talk_flag": 0x38},
         "five yellow keys"),
        ("floor12_store", 12, {3: 1500},
         {"shop_price": 1000, "shop_flag": 0x2e}, "store: no flag set"),
        ("floor28", 28, {8: 2},
         {"shop_price": 0, "shop_flag": 0x30}, "sell a key for 100"),
        ("floor28_nokey", 28, {},
         {"shop_price": 0, "shop_flag": 0x30}, "no key to sell"),
        ("floor45", 45, {3: 1000},
         {"shop_price": 1000, "shop_flag": 0x34}, "life +2000"),
        ("floor47", 47, {3: 4000},
         {"shop_price": 4000, "shop_flag": 0x35}, "super mattock"),
    ]
] + [
    Witness("button7click_floor6", "button7click", 0x44EA90, 6, 7, 4,
            aux_overrides={"shop_price": 0x32, "shop_flag": 0x2b},
            note="No: close the offer"),
    # The altar: handan case 0x10, then Button2Click (choice_mode 1) and
    # Button39..42Click.
    Witness("handan_altar_floor4", "handan", 0x444490, 4, 5, 1,
            tile=(5, 0x10), target_index=5, extra_state={3: 100},
            note="first offering: 20 gold"),
    Witness("handan_altar_floor4_third", "handan", 0x444490, 4, 5, 1,
            tile=(5, 0x10), target_index=5, extra_state={3: 100, 11: 2},
            note="third offering: 80 gold"),
    Witness("button2click_altar", "button2click", 0x44E1CC, 4, 5, 1,
            extra_state={3: 100}, aux_overrides={"choice_mode": 1, "shop_price": 20},
            note="Yes at the altar: the menu"),
    Witness("button39click_altar", "button39click", 0x451E1C, 4, 5, 1,
            extra_state={3: 100}, aux_overrides={"choice_mode": 1, "shop_price": 20},
            note="life"),
    Witness("button40click_altar", "button40click", 0x4520B4, 4, 5, 1,
            extra_state={3: 100, 11: 3}, aux_overrides={"choice_mode": 1, "shop_price": 20},
            note="attack"),
    Witness("button41click_altar_f15", "button41click", 0x452360, 15, 5, 1,
            extra_state={3: 500, 159: 1}, aux_overrides={"choice_mode": 1, "shop_price": 140},
            note="defense, area 2, option A"),
    Witness("button39click_altar_poor", "button39click", 0x451E1C, 4, 5, 1,
            extra_state={3: 10}, aux_overrides={"choice_mode": 1, "shop_price": 20},
            note="not enough gold"),
    Witness("button42click_altar", "button42click", 0x45260C, 4, 5, 1,
            aux_overrides={"choice_mode": 1, "shop_price": 20}, note="cancel"),
    # idou (0x44c04c): the step's adjacency checks (mazyutu1, madoushi1).
    Witness("idou_plain", "idou", 0x44C04C, 30, 5, 5,
            target_index=60, tiles=[(49, 0x6), (59, 0x6), (61, 0x6), (71, 0x6)],
            note="no neighbour: mmes again and disp"),
    Witness("idou_stairs_adj", "idou", 0x44C04C, 30, 5, 5,
            target_index=60, tiles=[(49, 0xb), (59, 0x6), (61, 0x6), (71, 0x6)], extra_state={5: 30},
            note="stairs up above on the top floor: state[0xa6] = 1"),
    Witness("idou_magician_6e", "idou", 0x44C04C, 30, 5, 5,
            target_index=60, tiles=[(49, 0x97), (59, 0x6), (61, 0x6), (71, 0x6)],
            note="magician 0x97 above: life -200"),
    Witness("idou_magician_6f_hard", "idou", 0x44C04C, 30, 5, 5,
            target_index=60, tiles=[(49, 0x6), (59, 0x6), (61, 0x99), (71, 0x6)], extra_state={159: 1},
            note="magician 0x99 right, difficulty 1: life -200, sprite 0x8b"),
    Witness("idou_magician_sacred", "idou", 0x44C04C, 30, 5, 5,
            target_index=60, tiles=[(49, 0x6), (59, 0x98), (61, 0x6), (71, 0x6)], extra_state={41: 1},
            note="magician left, Sacred Shield: no attack"),
    Witness("idou_magician_two", "idou", 0x44C04C, 30, 5, 5,
            target_index=60, tiles=[(49, 0x6), (59, 0x97), (61, 0x9a), (71, 0x6)],
            note="magicians left and right: two attacks"),
    Witness("idou_magician_edge", "idou", 0x44C04C, 30, 0, 6,
            target_index=66, tiles=[(55, 0x6), (65, 0x97), (67, 0x6), (77, 0x6)],
            note="x = 0: the left probe is off, no attack"),
    Witness("idou_f47_push", "idou", 0x44C04C, 47, 5, 5,
            target_index=60, tiles=[(38, 0x6), (49, 0x97), (59, 0x6), (61, 0x6), (71, 0x6)],
            note="floor 47: the magician is pushed up, then strikes"),
    Witness("idou_f47_blocked", "idou", 0x44C04C, 47, 5, 5,
            target_index=60, tiles=[(38, 0x20), (49, 0x97), (59, 0x6), (61, 0x6), (71, 0x6)],
            note="floor 47: the cell behind is not floor, no push"),
    Witness("idou_f47_push_down", "idou", 0x44C04C, 47, 5, 5,
            target_index=60, tiles=[(49, 0x6), (59, 0x6), (61, 0x6), (71, 0x98), (82, 0x6)],
            note="floor 47: magician below pushed down"),
    Witness("idou_f47_6f", "idou", 0x44C04C, 47, 5, 5,
            target_index=60, tiles=[(38, 0x6), (49, 0x99), (59, 0x6), (61, 0x6), (71, 0x6)],
            note="floor 47: 0x6f magicians are not pushed"),
    Witness("idou_pincer_h", "idou", 0x44C04C, 30, 5, 5,
            target_index=60, tiles=[(49, 0x6), (59, 0x5d), (61, 0x5d), (71, 0x6)],
            note="Magic Sergeants left and right: life halved"),
    Witness("idou_pincer_v", "idou", 0x44C04C, 30, 5, 5,
            target_index=60, tiles=[(49, 0x5d), (59, 0x6), (61, 0x6), (71, 0x5d)],
            note="Magic Sergeants above and below"),
    Witness("idou_pincer_both", "idou", 0x44C04C, 30, 5, 5,
            target_index=60, tiles=[(49, 0x5d), (59, 0x5d), (61, 0x5d), (71, 0x5d)],
            note="both pincers: halved twice"),
    Witness("idou_pincer_sacred", "idou", 0x44C04C, 30, 5, 5,
            target_index=60, tiles=[(49, 0x6), (59, 0x5d), (61, 0x5d), (71, 0x6)], extra_state={41: 1},
            note="Sacred Shield: no pincer"),
    Witness("idou_pincer_edge", "idou", 0x44C04C, 30, 0, 5,
            target_index=55, tiles=[(44, 0x6), (54, 0x5d), (56, 0x5d), (66, 0x6)],
            note="x = 0: no horizontal pincer"),
    Witness("idou_magician_death", "idou", 0x44C04C, 30, 5, 5,
            target_index=60, tiles=[(49, 0x97), (59, 0x6), (61, 0x6), (71, 0x6)], extra_state={0: 150},
            note="life 150: the magician kills, try again: Yes"),
    Witness("idou_magician_pincer", "idou", 0x44C04C, 30, 5, 5,
            target_index=60, tiles=[(49, 0x97), (59, 0x5d), (61, 0x5d), (71, 0x6)],
            note="magician above and a pincer"),
    Witness("idou_pincer_facing1", "idou", 0x44C04C, 30, 5, 5,
            target_index=60, tiles=[(49, 0x6), (59, 0x5d), (61, 0x5d), (71, 0x6)],
            extra_state={0x58: 1}, note="pincer, facing 1: hit pose 0xbf/0xc0"),
    Witness("idou_magician_facing1", "idou", 0x44C04C, 30, 5, 5,
            target_index=60, tiles=[(49, 0x97), (59, 0x6), (61, 0x6), (71, 0x6)],
            extra_state={0x58: 1}, note="magician, facing 1"),
    Witness("idou_pincer_facing2", "idou", 0x44C04C, 30, 5, 5,
            target_index=60, tiles=[(49, 0x6), (59, 0x5d), (61, 0x5d), (71, 0x6)],
            extra_state={0x58: 2}, note="pincer, facing 2: hit pose 0xc7/0xc8"),
    Witness("idou_magician_facing2", "idou", 0x44C04C, 30, 5, 5,
            target_index=60, tiles=[(49, 0x97), (59, 0x6), (61, 0x6), (71, 0x6)],
            extra_state={0x58: 2}, note="magician, facing 2"),
    Witness("idou_pincer_facing3", "idou", 0x44C04C, 30, 5, 5,
            target_index=60, tiles=[(49, 0x6), (59, 0x5d), (61, 0x5d), (71, 0x6)],
            extra_state={0x58: 3}, note="pincer, facing 3: hit pose 0xcf/0xd0"),
    Witness("idou_magician_facing3", "idou", 0x44C04C, 30, 5, 5,
            target_index=60, tiles=[(49, 0x97), (59, 0x6), (61, 0x6), (71, 0x6)],
            extra_state={0x58: 3}, note="magician, facing 3"),
    # moncheck (0x4664e4): after every won battle (splice 0x0b/5).
    Witness("moncheck_floor2", "moncheck", 0x4664E4, 2, 5, 5,
            tiles=[(0x10, 6), (0x12, 6), (0x30, 5)],
            note="floor 2: guards gone -> six doors (kind 6)"),
    Witness("moncheck_floor2_not", "moncheck", 0x4664E4, 2, 5, 5,
            tiles=[(0x10, 6), (0x12, 0x3d), (0x30, 5)],
            note="floor 2: a guard left"),
    Witness("moncheck_floor8", "moncheck", 0x4664E4, 8, 5, 5,
            tiles=[(0x34, 6), (0x36, 6), (0x2a, 4)],
            note="floor 8: door 0x2a opens"),
    Witness("moncheck_floor10_gate", "moncheck", 0x4664E4, 10, 5, 4,
            tiles=[(i, 6) for i in (37, 38, 39, 48, 50, 59, 60, 61)]
                  + [(27, 4), (5, 0x51)],
            note="floor 10: soldiers dead -> gate, sub-event 24"),
    Witness("moncheck_floor10_knight", "moncheck", 0x4664E4, 10, 5, 4,
            tiles=[(5, 6), (71, 4), (40, 4), (36, 4)],
            note="floor 10: knight dead -> rewards, fairy, sub-event 25"),
    Witness("moncheck_floor10_both_sound", "moncheck", 0x4664E4, 10, 5, 4,
            tiles=[(i, 6) for i in (37, 38, 39, 48, 50, 59, 60, 61)]
                  + [(27, 4), (5, 6), (71, 4), (40, 4), (36, 4)],
            sound_state=5, note="floor 10: both, sound on"),
    Witness("moncheck_floor40", "moncheck", 0x4664E4, 40, 5, 5,
            tiles=[(5, 0x06), (16, 0x0b)],
            note="floor 40: tile 7 + tile 18 = 0x11, sound off"),
    Witness("moncheck_floor40_sound", "moncheck", 0x4664E4, 40, 5, 5,
            tiles=[(5, 0x06), (16, 0x0b)], sound_state=1,
            note="floor 40: same, sound on (adds sound 6)"),
    Witness("moncheck_floor40_done", "moncheck", 0x4664E4, 40, 5, 5,
            tiles=[(5, 0x06), (16, 0x0b)], extra_state={0x96: 1},
            note="floor 40: one-shot state[0x96] already set"),
    Witness("moncheck_floor40_not", "moncheck", 0x4664E4, 40, 5, 5,
            tiles=[(5, 0x06), (16, 0x06)],
            note="floor 40: tile sum mismatch"),
    Witness("moncheck_floor5", "moncheck", 0x4664E4, 5, 5, 5,
            note="no moncheck case on floor 5"),
    Witness("moncheck_floor11", "moncheck", 0x4664E4, 11, 5, 5,
            tiles=[(0x2c, 0x6), (0x2e, 0x6), (0x22, 0x4)],
            note="floor 11: door 0x22"),
    Witness("moncheck_floor11_not", "moncheck", 0x4664E4, 11, 5, 5,
            tiles=[(0x2c, 0x6), (0x2e, 0x20), (0x22, 0x4)],
            note="floor 11: a monster left"),
    Witness("moncheck_floor14", "moncheck", 0x4664E4, 14, 5, 5,
            tiles=[(0x0, 0x6), (0x2, 0x6), (0xc, 0x6), (0x16, 0x8)],
            note="floor 14: tile 0x16 -> 0x1e"),
    Witness("moncheck_floor15", "moncheck", 0x4664E4, 15, 5, 5,
            tiles=[(0x3c, 0x6), (0x1b, 0x4), (0x5d, 0x4)],
            note="floor 15: tile 0x31, doors 0x5d/0x1b"),
    Witness("moncheck_floor17_all", "moncheck", 0x4664E4, 17, 5, 5,
            tiles=[(0x34, 0x6), (0x36, 0x6), (0x2a, 0x4), (0x55, 0x6), (0x57, 0x6), (0x4b, 0x4), (0x2c, 0x6), (0x2e, 0x6), (0x22, 0x4), (0x4d, 0x6), (0x4f, 0x6), (0x43, 0x4)],
            note="floor 17: all four doors"),
    Witness("moncheck_floor17_two", "moncheck", 0x4664E4, 17, 5, 5,
            tiles=[(0x34, 0x6), (0x36, 0x6), (0x2a, 0x4), (0x55, 0x6), (0x57, 0x20), (0x4b, 0x4), (0x2c, 0x20), (0x2e, 0x6), (0x22, 0x4), (0x4d, 0x6), (0x4f, 0x6), (0x43, 0x4)],
            note="floor 17: first and last doors"),
    Witness("moncheck_floor20", "moncheck", 0x4664E4, 20, 5, 5,
            tiles=[(0x30, 0x6), (0x31, 0x6), (0x32, 0x6), (0x3b, 0x6), (0x3d, 0x6), (0x46, 0x6), (0x47, 0x6), (0x48, 0x6), (0x3c, 0x6), (0x1b, 0x4), (0x5d, 0x4)],
            note="floor 20: vampire room cleared, sound off"),
    Witness("moncheck_floor20_sound", "moncheck", 0x4664E4, 20, 5, 5,
            tiles=[(0x30, 0x6), (0x31, 0x6), (0x32, 0x6), (0x3b, 0x6), (0x3d, 0x6), (0x46, 0x6), (0x47, 0x6), (0x48, 0x6), (0x3c, 0x6), (0x1b, 0x4), (0x5d, 0x4)], sound_state=1,
            note="floor 20: same, sound on"),
    Witness("moncheck_floor25", "moncheck", 0x4664E4, 25, 5, 5,
            tiles=[(0x3c, 0x6), (0x68, 0x4)],
            note="floor 25: room cleared"),
    Witness("moncheck_floor25_f24", "moncheck", 0x4664E4, 25, 5, 5,
            tiles=[(0x3c, 0x6), (0x68, 0x4)], extra_state={116: 1},
            note="floor 25: state[0x74] = 1 rewrites floor 24"),
    Witness("moncheck_floor30", "moncheck", 0x4664E4, 30, 5, 5,
            tiles=[(0x2e, 0x6), (0x2f, 0x6), (0x30, 0x6), (0x32, 0x6), (0x33, 0x6), (0x34, 0x6), (0x26, 0x4), (0x47, 0x4)],
            note="floor 30: stairs, doors 0x47/0x26"),
    Witness("moncheck_floor32_door", "moncheck", 0x4664E4, 32, 5, 5,
            tiles=[(0x63, 0x6), (0x65, 0x6), (0x59, 0x4), (0x3c, 0x20)],
            note="floor 32: door 0x59 only"),
    Witness("moncheck_floor32_thief", "moncheck", 0x4664E4, 32, 5, 5,
            tiles=[(0x63, 0x6), (0x65, 0x20), (0x59, 0x4), (0x3c, 0x6)], extra_state={120: 1},
            note="floor 32: tile 0x3c + state[0x78] + state[0x79] = 7"),
    Witness("moncheck_floor33", "moncheck", 0x4664E4, 33, 5, 5,
            tiles=[(0x34, 0x6), (0x36, 0x6), (0x4a, 0x6), (0x4c, 0x6), (0x2a, 0x4), (0x56, 0x4)],
            note="floor 33: doors 0x56/0x2a"),
    Witness("moncheck_floor34", "moncheck", 0x4664E4, 34, 5, 5,
            tiles=[(0x25, 0x6), (0x27, 0x6), (0x29, 0x6), (0x2b, 0x6), (0x51, 0x6), (0x53, 0x6), (0x55, 0x6), (0x57, 0x6)],
            note="floor 34: one-shot state[0x94]"),
    Witness("moncheck_floor34_done", "moncheck", 0x4664E4, 34, 5, 5,
            tiles=[(0x25, 0x6), (0x27, 0x6), (0x29, 0x6), (0x2b, 0x6), (0x51, 0x6), (0x53, 0x6), (0x55, 0x6), (0x57, 0x6)], extra_state={148: 1},
            note="floor 34: already done"),
    Witness("moncheck_floor35", "moncheck", 0x4664E4, 35, 5, 5,
            tiles=[(0x47, 0x6), (0x1b, 0x4)],
            note="floor 35: one-shot state[0x95]"),
    Witness("moncheck_floor38", "moncheck", 0x4664E4, 38, 5, 5,
            tiles=[(0x63, 0x6), (0x65, 0x6)],
            note="floor 38: tile 0x59 -> 9, no commands"),
    Witness("moncheck_floor41", "moncheck", 0x4664E4, 41, 5, 5,
            tiles=[(0xc, 0x6), (0x14, 0x6)],
            note="floor 41: one-shot state[0x9a]"),
    Witness("moncheck_floor44", "moncheck", 0x4664E4, 44, 5, 5,
            tiles=[(0x5c, 0x6), (0x5e, 0x6), (0x52, 0x4)],
            note="floor 44: door 0x52"),
    Witness("moncheck_floor45", "moncheck", 0x4664E4, 45, 5, 5,
            tiles=[(0x5c, 0x6), (0x72, 0x6), (0x66, 0x4), (0x5f, 0x6), (0x75, 0x6), (0x69, 0x4)],
            note="floor 45: doors 0x66/0x69"),
    Witness("moncheck_floor48", "moncheck", 0x4664E4, 48, 5, 5,
            tiles=[(0x0, 0x6), (0x54, 0x4)],
            note="floor 48: door 0x54"),
    Witness("moncheck_floor49_doors", "moncheck", 0x4664E4, 49, 5, 5,
            tiles=[(0x67, 0x6), (0x69, 0x6), (0x5d, 0x4), (0x51, 0x6), (0x53, 0x6), (0x47, 0x4), (0x3c, 0x7), (0x1b, 0x20)],
            note="floor 49: doors 0x5d/0x47"),
    Witness("moncheck_floor49_sergeants", "moncheck", 0x4664E4, 49, 5, 5,
            tiles=[(0x10, 0x6), (0x1a, 0x6), (0x1c, 0x6), (0x26, 0x6), (0xf, 0x5d), (0x11, 0x5d), (0x25, 0x5d), (0x27, 0x5d), (0x1b, 0x20)], sound_state=1,
            note="floor 49: four Magic Sergeants, sound on"),
    Witness("moncheck_floor49_zeno", "moncheck", 0x4664E4, 49, 5, 5,
            tiles=[(0x1b, 0x6), (0x47, 0x4), (0x3c, 0x6)], sound_state=1,
            note="floor 49: Zeno gone, sound on"),
    Witness("moncheck_floor50", "moncheck", 0x4664E4, 50, 5, 5,
            tiles=[], extra_state={159: 1},
            note="floor 50: option A set -> ending"),
    Witness("moncheck_floor50_not", "moncheck", 0x4664E4, 50, 5, 5,
            tiles=[],
            note="floor 50: option A clear"),
    Witness("moncheck_floor8_not", "moncheck", 0x4664E4, 8, 5, 5,
            tiles=[(0x34, 0x6), (0x36, 0x20), (0x2a, 0x4)],
            note="floor 8: a monster left"),
    Witness("moncheck_floor14_not", "moncheck", 0x4664E4, 14, 5, 5,
            tiles=[(0x0, 0x6), (0x2, 0x6), (0xc, 0x20), (0x16, 0x8)],
            note="floor 14: a monster left"),
    Witness("moncheck_floor15_not", "moncheck", 0x4664E4, 15, 5, 5,
            tiles=[(0x3c, 0x20), (0x1b, 0x4), (0x5d, 0x4)],
            note="floor 15: a monster left"),
    Witness("moncheck_floor17_middle", "moncheck", 0x4664E4, 17, 5, 5,
            tiles=[(0x34, 0x6), (0x36, 0x20), (0x2a, 0x4), (0x55, 0x6), (0x57, 0x6), (0x4b, 0x4), (0x2c, 0x6), (0x2e, 0x6), (0x22, 0x4), (0x4d, 0x20), (0x4f, 0x6), (0x43, 0x4)],
            note="floor 17: only the middle two doors"),
    Witness("moncheck_floor20_not", "moncheck", 0x4664E4, 20, 5, 5,
            tiles=[(0x30, 0x6), (0x31, 0x6), (0x32, 0x6), (0x3b, 0x6), (0x3d, 0x6), (0x46, 0x6), (0x47, 0x6), (0x48, 0x6), (0x3c, 0x20), (0x1b, 0x4), (0x5d, 0x4)],
            note="floor 20: the vampire left"),
    Witness("moncheck_floor25_not", "moncheck", 0x4664E4, 25, 5, 5,
            tiles=[(0x3c, 0x20), (0x68, 0x4)], extra_state={116: 1},
            note="floor 25: a monster left"),
    Witness("moncheck_floor30_not", "moncheck", 0x4664E4, 30, 5, 5,
            tiles=[(0x2e, 0x6), (0x2f, 0x6), (0x30, 0x6), (0x32, 0x6), (0x33, 0x6), (0x34, 0x20), (0x26, 0x4), (0x47, 0x4)],
            note="floor 30: a monster left"),
    Witness("moncheck_floor33_not", "moncheck", 0x4664E4, 33, 5, 5,
            tiles=[(0x34, 0x6), (0x36, 0x6), (0x4a, 0x6), (0x4c, 0x20), (0x2a, 0x4), (0x56, 0x4)],
            note="floor 33: a monster left"),
    Witness("moncheck_floor34_not", "moncheck", 0x4664E4, 34, 5, 5,
            tiles=[(0x25, 0x6), (0x27, 0x6), (0x29, 0x6), (0x2b, 0x6), (0x51, 0x6), (0x53, 0x6), (0x55, 0x6), (0x57, 0x20)],
            note="floor 34: a monster left"),
    Witness("moncheck_floor35_not", "moncheck", 0x4664E4, 35, 5, 5,
            tiles=[(0x47, 0x20), (0x1b, 0x4)],
            note="floor 35: a monster left"),
    Witness("moncheck_floor35_done", "moncheck", 0x4664E4, 35, 5, 5,
            tiles=[(0x47, 0x6), (0x1b, 0x4)], extra_state={149: 1},
            note="floor 35: already done"),
    Witness("moncheck_floor38_not", "moncheck", 0x4664E4, 38, 5, 5,
            tiles=[(0x63, 0x6), (0x65, 0x20)],
            note="floor 38: a monster left"),
    Witness("moncheck_floor41_not", "moncheck", 0x4664E4, 41, 5, 5,
            tiles=[(0xc, 0x6), (0x14, 0x20)],
            note="floor 41: a monster left"),
    Witness("moncheck_floor41_done", "moncheck", 0x4664E4, 41, 5, 5,
            tiles=[(0xc, 0x6), (0x14, 0x6)], extra_state={154: 1},
            note="floor 41: already done"),
    Witness("moncheck_floor44_not", "moncheck", 0x4664E4, 44, 5, 5,
            tiles=[(0x5c, 0x20), (0x5e, 0x6), (0x52, 0x4)],
            note="floor 44: a monster left"),
    Witness("moncheck_floor45_second", "moncheck", 0x4664E4, 45, 5, 5,
            tiles=[(0x5c, 0x6), (0x72, 0x20), (0x66, 0x4), (0x5f, 0x6), (0x75, 0x6), (0x69, 0x4)],
            note="floor 45: only door 0x69"),
    Witness("moncheck_floor45_first", "moncheck", 0x4664E4, 45, 5, 5,
            tiles=[(0x5c, 0x6), (0x72, 0x6), (0x66, 0x4), (0x5f, 0x6), (0x75, 0x20), (0x69, 0x4)],
            note="floor 45: only door 0x66"),
    Witness("moncheck_floor48_not", "moncheck", 0x4664E4, 48, 5, 5,
            tiles=[(0x0, 0x20), (0x54, 0x4)],
            note="floor 48: a monster left"),
    Witness("moncheck_floor49_sergeant_left", "moncheck", 0x4664E4, 49, 5, 5,
            tiles=[(0x10, 0x6), (0x1a, 0x6), (0x1c, 0x6), (0x26, 0x6), (0xf, 0x5d), (0x11, 0x5d), (0x25, 0x5d), (0x27, 0x6), (0x1b, 0x20)],
            note="floor 49: ring clear, a Magic Sergeant gone"),
    Witness("moncheck_floor49_sergeants_quiet", "moncheck", 0x4664E4, 49, 5, 5,
            tiles=[(0x10, 0x6), (0x1a, 0x6), (0x1c, 0x6), (0x26, 0x6), (0xf, 0x5d), (0x11, 0x5d), (0x25, 0x5d), (0x27, 0x5d), (0x1b, 0x20)], extra_state={156: 1},
            note="floor 49: Magic Sergeants, sound off, state[0x9c] already set"),
    Witness("moncheck_floor49_zeno_quiet", "moncheck", 0x4664E4, 49, 5, 5,
            tiles=[(0x1b, 0x6), (0x47, 0x4), (0x3c, 0x6)],
            note="floor 49: Zeno gone, sound off"),
    Witness("moncheck_floor51", "moncheck", 0x4664E4, 51, 5, 5,
            tiles=[],
            note="floor 51: past the switch (ja 0x46cb09)"),
    # handan cases ported with the builders: hidden passages (0x0a), raw
    # 0x1b/0x1c, doorcheck extras, raw 0x24, inert raws.
    Witness("handan_passage_f12", "handan", 0x444490, 12, 10, 1,
            target_index=10, tiles=[(10, 0xa)],
            note="floor 12: passage at 10 -> 0x13"),
    Witness("handan_passage_f12_other", "handan", 0x444490, 12, 5, 6,
            target_index=60, tiles=[(60, 0xa)],
            note="floor 12: another 0x0a cell: nothing"),
    Witness("handan_passage_f16", "handan", 0x444490, 16, 10, 9,
            target_index=120, tiles=[(120, 0xa)],
            note="floor 16: passage at 0x78 -> 0x19"),
    Witness("handan_passage_f19", "handan", 0x444490, 19, 5, 3,
            target_index=27, tiles=[(27, 0xa)],
            note="floor 19: passage at 0x1b -> 0x27"),
    Witness("handan_passage_f41", "handan", 0x444490, 41, 5, 6,
            target_index=60, tiles=[(60, 0xa), (12, 0x6)],
            note="floor 41: cell 12 floor -> magician at 0x14"),
    Witness("handan_passage_f41_not", "handan", 0x444490, 41, 5, 6,
            target_index=60, tiles=[(60, 0xa), (12, 0x20)],
            note="floor 41: cell 12 not floor: nothing"),
    Witness("handan_passage_f5", "handan", 0x444490, 5, 5, 6,
            target_index=60, tiles=[(60, 0xa)],
            note="floor 5: no passage case"),
    Witness("handan_raw1b", "handan", 0x444490, 24, 5, 6,
            target_index=60, tiles=[(60, 0x1b)],
            note="raw 0x1b: floor-50 cell, sub-events 36/35"),
    Witness("handan_raw1c_sound", "handan", 0x444490, 24, 5, 6,
            target_index=60, tiles=[(60, 0x1c)], sound_state=1,
            note="raw 0x1c with sound on: BGM 13"),
    Witness("handan_raw1b_bgm13", "handan", 0x444490, 24, 5, 6,
            target_index=60, tiles=[(60, 0x1b)], sound_state=13,
            note="raw 0x1b with BGM 13 already"),
    Witness("handan_raw24", "handan", 0x444490, 5, 5, 6,
            target_index=60, tiles=[(60, 0x24)],
            note="raw 0x24: no item case, only the pickup tail"),
    Witness("handan_raw0d", "handan", 0x444490, 5, 5, 6,
            target_index=60, tiles=[(60, 0xd)],
            note="raw 0x0d (lava): nothing"),
    Witness("handan_raw00", "handan", 0x444490, 5, 5, 6,
            target_index=60, tiles=[(60, 0x0)],
            note="raw 0x00: nothing"),
    Witness("handan_door_f39_a", "handan", 0x444490, 39, 5, 4,
            target_index=38, tiles=[(12, 0x1), (16, 0x1), (34, 0x1), (36, 0x1), (56, 0x1), (58, 0x1), (60, 0x1), (14, 0x6), (38, 0x1)], extra_state={8: 1},
            note="floor 39: door 0x26 with the ring of 1s and 14 clear: wall 0x24"),
    Witness("handan_door_f39_b", "handan", 0x444490, 39, 3, 0,
            target_index=14, tiles=[(12, 0x1), (16, 0x1), (34, 0x1), (36, 0x1), (56, 0x1), (58, 0x1), (60, 0x1), (38, 0x6), (14, 0x1)], extra_state={8: 1},
            note="floor 39: door 0x0e with 38 clear"),
    Witness("handan_door_f39_nokey", "handan", 0x444490, 39, 5, 4,
            target_index=38, tiles=[(12, 0x1), (16, 0x1), (34, 0x1), (36, 0x1), (56, 0x1), (58, 0x1), (60, 0x1), (14, 0x6), (38, 0x1)],
            note="floor 39: same without a key"),
    Witness("handan_door_f39_ring_open", "handan", 0x444490, 39, 5, 4,
            target_index=38, tiles=[(12, 0x1), (16, 0x1), (34, 0x1), (36, 0x1), (56, 0x1), (58, 0x6), (60, 0x1), (14, 0x6), (38, 0x1)], extra_state={8: 1},
            note="floor 39: a ring door already open: no wall"),
    Witness("handan_door_f43", "handan", 0x444490, 43, 7, 1,
            target_index=7, tiles=[(7, 0x1), (8, 0x5d)], extra_state={8: 1},
            note="floor 43: door 7 next to a Magic Sergeant"),
    Witness("handan_gem_hard", "handan", 0x444490, 25, 5, 6,
            target_index=60, tiles=[(60, 0x22)], extra_state={159: 1},
            note="attack gem on floor 25, difficulty 1"),
    # handan monster raws -> taisen (0x44a480), ordinary battles.
    Witness("battle_zombie", "handan", 0x444490, 20, 5, 6,
            target_index=60, tiles=[(49, 0x6), (59, 0x6), (61, 0x6), (71, 0x6), (60, 0x51)], extra_state={1: 300, 2: 300},
            note="Zombie, no Cross"),
    Witness("battle_zombie_cross", "handan", 0x444490, 20, 5, 6,
            target_index=60, tiles=[(49, 0x6), (59, 0x6), (61, 0x6), (71, 0x6), (60, 0x51)], extra_state={1: 300, 2: 300, 20: 1},
            note="Zombie with the Cross: double attack"),
    Witness("battle_vampire_cross", "handan", 0x444490, 20, 5, 6,
            target_index=60, tiles=[(49, 0x6), (59, 0x6), (61, 0x6), (71, 0x6), (60, 0x5f)], extra_state={1: 300, 2: 300, 20: 1},
            note="Vampire (raw 0x5f) with the Cross"),
    Witness("battle_octopus_centre", "handan", 0x444490, 15, 5, 6,
            target_index=60, tiles=[(59, 0x6), (61, 0x6), (71, 0x6), (60, 0x68), (49, 0x64)], extra_state={1: 3000, 2: 3000},
            note="Giant Octopus centre: nine cells cleared, op 0x0e"),
    Witness("battle_octopus_arm", "handan", 0x444490, 15, 5, 6,
            target_index=60, tiles=[(49, 0x6), (59, 0x6), (61, 0x6), (71, 0x6), (60, 0x63)], extra_state={1: 3000, 2: 3000},
            note="Giant Octopus outer cell: half gold"),
    Witness("battle_octopus_arm_lucky", "handan", 0x444490, 15, 5, 6,
            target_index=60, tiles=[(49, 0x6), (59, 0x6), (61, 0x6), (71, 0x6), (60, 0x63)], extra_state={1: 3000, 2: 3000, 31: 1},
            note="same with Lucky Gold"),
    Witness("battle_dragon", "handan", 0x444490, 35, 5, 6,
            target_index=60, tiles=[(49, 0x6), (59, 0x6), (61, 0x6), (71, 0x6), (60, 0x7a)], extra_state={1: 3000, 2: 3000},
            note="Dragon centre"),
    Witness("battle_dragon_slayer", "handan", 0x444490, 35, 5, 6,
            target_index=60, tiles=[(49, 0x6), (59, 0x6), (61, 0x6), (71, 0x6), (60, 0x7a)], extra_state={1: 1000, 2: 600, 27: 1},
            note="Dragon with the Dragon Slayer"),
    Witness("battle_high_raw", "handan", 0x444490, 40, 5, 6,
            target_index=60, tiles=[(49, 0x6), (59, 0x6), (61, 0x6), (71, 0x6), (60, 0x87)], extra_state={1: 3000, 2: 3000},
            note="raw 0x87 (second monster range)"),
    Witness("battle_sergeant", "handan", 0x444490, 40, 5, 6,
            target_index=60, tiles=[(49, 0x6), (59, 0x6), (61, 0x6), (71, 0x6), (60, 0x5d)], extra_state={1: 3000, 2: 3000},
            note="Magic Sergeant raw 0x5d"),
    Witness("battle_hard", "handan", 0x444490, 5, 5, 6,
            target_index=60, tiles=[(49, 0x6), (59, 0x6), (61, 0x6), (71, 0x6), (60, 0x3d)], extra_state={159: 1},
            note="Green Slime on difficulty 1"),
    Witness("battle_lucky", "handan", 0x444490, 5, 5, 6,
            target_index=60, tiles=[(49, 0x6), (59, 0x6), (61, 0x6), (71, 0x6), (60, 0x41)], extra_state={31: 1},
            note="Big Slime with Lucky Gold"),
    Witness("battle_blocked", "handan", 0x444490, 5, 5, 6,
            target_index=60, tiles=[(49, 0x6), (59, 0x6), (61, 0x6), (71, 0x6), (60, 0x9d)], extra_state={1: 100},
            note="Gate-Keeper A (raw 0x9d) blocks every attack"),
    Witness("battle_lose", "handan", 0x444490, 20, 5, 6,
            target_index=60, tiles=[(49, 0x6), (59, 0x6), (61, 0x6), (71, 0x6), (60, 0x51)], extra_state={0: 100, 1: 90, 2: 10},
            note="Zombie kills: the death window, try again: Yes"),
    Witness("battle_magician", "handan", 0x444490, 30, 5, 6,
            target_index=60, tiles=[(49, 0x97), (59, 0x6), (61, 0x6), (71, 0x6), (60, 0x3d)], extra_state={1: 3000, 2: 3000},
            note="battle step next to a magician"),
    Witness("battle_zeno_f49", "handan", 0x444490, 49, 5, 6,
            target_index=60, tiles=[(49, 0x6), (59, 0x6), (61, 0x6), (71, 0x6), (60, 0x5b)], extra_state={1: 20000, 2: 20000},
            note="floor 49: Zeno fought"),
    Witness("battle_zeno_f50_ending", "handan", 0x444490, 50, 5, 6,
            target_index=60, tiles=[(49, 0x6), (59, 0x6), (61, 0x6), (71, 0x6), (60, 0x5c)],
            note="floor 50, first round: the ending timeline"),
    Witness("battle_zeno_f50_round2", "handan", 0x444490, 50, 5, 6,
            target_index=60, tiles=[(49, 0x6), (59, 0x6), (61, 0x6), (71, 0x6), (60, 0x5c)], extra_state={159: 1, 1: 3000, 2: 3000},
            note="floor 50, later round: Zeno 1000/625/125"),
    Witness("battle_zeno_f50_carry", "handan", 0x444490, 50, 5, 6,
            target_index=60, tiles=[(49, 0x6), (59, 0x6), (61, 0x6), (71, 0x6), (60, 0x5c)], extra_state={159: 1, 160: 1, 1: 20000, 2: 20000},
            note="floor 50, later round, carried over: table Zeno"),
    Witness("battle_zeno_f30", "handan", 0x444490, 30, 5, 6,
            target_index=60, tiles=[(49, 0x6), (59, 0x6), (61, 0x6), (71, 0x6), (60, 0x5b)],
            note="raw 0x5b elsewhere: nothing"),
    # handan 0x0b/0x0c: stairs (kaidanwork, kaidanup/downcheck).
    Witness("stairs_up_43", "handan", 0x444490, 43, 5, 6,
            target_index=60, tiles=[(60, 0xb)], extra_state={5: 50},
            note="floor 43 up: to 45"),
    Witness("stairs_down_45", "handan", 0x444490, 45, 5, 6,
            target_index=60, tiles=[(60, 0xc)], extra_state={5: 50},
            note="floor 45 down: to 43"),
    Witness("stairs_up_sound", "handan", 0x444490, 10, 5, 6,
            target_index=60, tiles=[(60, 0xb)], extra_state={5: 50}, sound_state=3,
            note="up with sound on: BGM at step 20"),
    Witness("stairs_up_f20_fairy", "handan", 0x444490, 20, 5, 6,
            target_index=60, tiles=[(60, 0xb), (49, 0x17)], extra_state={5: 50},
            note="floor 20 up: the fairy cell 49 cleared"),
    Witness("stairs_up_f25_fairy", "handan", 0x444490, 25, 5, 6,
            target_index=60, tiles=[(60, 0xb), (71, 0x17)], extra_state={5: 50},
            note="floor 25 up: cell 71 cleared"),
    Witness("stairs_down_f25_fairy", "handan", 0x444490, 25, 5, 6,
            target_index=60, tiles=[(60, 0xc), (71, 0x17)], extra_state={5: 50},
            note="floor 25 down: cell 71 cleared"),
    Witness("stairs_up_f40_fairy", "handan", 0x444490, 40, 5, 6,
            target_index=60, tiles=[(60, 0xb), (60, 0xb)], extra_state={5: 50},
            note="floor 40 up"),
    Witness("stairs_down_f49", "handan", 0x444490, 49, 5, 6,
            target_index=60, tiles=[(60, 0xc), (16, 0x17)], extra_state={5: 50},
            note="floor 49 down: cell 16 cleared"),
    Witness("stairs_up_new_35", "handan", 0x444490, 34, 5, 6,
            target_index=60, tiles=[(60, 0xb)], extra_state={5: 34, 35: 35},
            note="first visit to 35 with state[35] = 0x23"),
    Witness("stairs_down_29_28", "handan", 0x444490, 29, 5, 6,
            target_index=60, tiles=[(60, 0xc)], extra_state={5: 29},
            note="floor 29 down with limit 29: floor-29 cells"),
    Witness("stairs_up_to_32", "handan", 0x444490, 31, 5, 6,
            target_index=60, tiles=[(60, 0xb)], extra_state={5: 50},
            note="to floor 32: its mevent once"),
    Witness("stairs_up_to_32_done", "handan", 0x444490, 31, 5, 6,
            target_index=60, tiles=[(60, 0xb)], extra_state={5: 50, 155: 1},
            note="to floor 32, mevent done"),
    Witness("stairs_up_to_42", "handan", 0x444490, 41, 5, 6,
            target_index=60, tiles=[(60, 0xb)], extra_state={5: 50},
            note="to floor 42: its mevent once"),
    Witness("mevent_floor23", "mevent", 0x46397C, 23, 5, 6,
            target_index=60, tile=(60, 0x07), note="mevent floor 23: wall 8 at the target"),
    Witness("handan_mevent_floor23", "handan", 0x444490, 23, 5, 6,
            target_index=60, tile=(60, 0x07), note="handan raw 0x07 -> mevent floor 23"),
    Witness("mevent_floor7", "mevent", 0x46397C, 7, 5, 6,
            target_index=60, tile=(60, 0x07), note="no mevent case on floor 7"),
    # touzoku (0x465900): the thief, first met as the floor-2 prisoner.
    Witness("touzoku_floor2_cell", "touzoku", 0x465900, 2, 2, 7,
            tile=(68, 0x15), target_index=68,
            note="prisoner in the cell: sub-event 14, cell door opens"),
    Witness("touzoku_floor2_cell_sound", "touzoku", 0x465900, 2, 3, 6,
            tile=(68, 0x15), target_index=68, sound_state=1,
            note="same from (3,6) with sound on"),
    Witness("handan_touzoku_floor2_cell", "handan", 0x444490, 2, 2, 7,
            tile=(68, 0x15), target_index=68,
            note="handan raw 0x15 -> touzoku from (2,7)"),
    Witness("touzoku_floor2_tunnel", "touzoku", 0x465900, 2, 0, 8,
            tile=(77, 0x15), target_index=77,
            note="prisoner at the tunnel: sub-event 15"),
    Witness("handan_touzoku_floor2_tunnel", "handan", 0x444490, 2, 0, 8,
            tile=(77, 0x15), target_index=77,
            note="handan raw 0x15 -> touzoku from (0,8)"),
    Witness("touzoku_floor2_x9", "touzoku", 0x465900, 2, 9, 1,
            note="later floor-2 meeting: sub-event 16"),
    Witness("touzoku_floor2_x9_flag", "touzoku", 0x465900, 2, 10, 1,
            extra_state={0x60: 1}, note="state[0x60] = 1: sub-event 17"),
    Witness("touzoku_floor15", "touzoku", 0x465900, 15, 5, 5,
            note="floor 15: moves, sub-event 0x1c, door at 7"),
    Witness("touzoku_floor29_pos", "touzoku", 0x465900, 29, 5, 2,
            note="floor 29 at index 0x1b: sub-event 0x25"),
    Witness("touzoku_floor29", "touzoku", 0x465900, 29, 5, 5,
            note="floor 29, state[0x76] = 0: sub-event 0x26"),
    Witness("touzoku_floor29_flag", "touzoku", 0x465900, 29, 5, 5,
            extra_state={0x76: 1}, note="state[0x76] = 1: sub-event 0x27"),
    Witness("touzoku_floor35", "touzoku", 0x465900, 35, 5, 5,
            note="floor 35: g_map[0x16e] = 6, sub-event 0x2a"),
    Witness("touzoku_floor35_flag", "touzoku", 0x465900, 35, 5, 5,
            extra_state={0x7a: 1}, note="state[0x7a] = 1: sub-event 0x2b"),
    Witness("touzoku_floor50", "touzoku", 0x465900, 50, 5, 5,
            note="floor 50: sub-event 0x112"),
    Witness("touzoku_floor7", "touzoku", 0x465900, 7, 5, 5,
            note="no thief case on this floor"),
    # openning31 (0x4653b8): the capture, timeline sub-dispatch 0x0b/1.
    Witness("openning31", "openning31", 0x4653B8, 3, 4, 8,
            note="normal run: 400/10/10, prison (2,7)"),
    Witness("openning31_r", "openning31", 0x4653B8, 3, 4, 8,
            extra_state={159: 1, 17: 1, 18: 1, 20: 1, 31: 1, 36: 1, 41: 1,
                         15: 3, 16: 3},
            note="second run (option A = 1): items taken instead"),
] + [
    # Ordinary map actions through handan (0x444490), the reference for the
    # C movement code (tests/test_game_parity.c): the player at (5,6)
    # steps up onto index 60 (5,5).
    Witness(f"action_{name}", "handan", 0x444490, floor, 5, 6,
            tile=(60, raw), target_index=60, extra_state=extra, note=note)
    for name, floor, raw, extra, note in [
        ("door_yellow", 5, 0x01, {8: 1}, "yellow door with a key"),
        ("door_yellow_nokey", 5, 0x01, {}, "yellow door without a key"),
        ("door_blue", 5, 0x02, {10: 1}, "blue door with a key"),
        ("door_blue_nokey", 5, 0x02, {}, "blue door without a key"),
        ("door_red", 5, 0x03, {9: 1}, "red door with a key"),
        ("door_red_nokey", 5, 0x03, {}, "red door without a key"),
        ("key_yellow", 5, 0x1d, {}, "yellow key"),
        ("key_red", 5, 0x1e, {}, "red key"),
        ("key_blue", 5, 0x1f, {}, "blue key"),
        ("floor", 5, 0x06, {}, "plain floor"),
        ("stairs_up", 5, 0x0b, {5: 5}, "up stairs"),
        ("stairs_down", 5, 0x0c, {5: 5}, "down stairs"),
    ] + [
        (f"item_{raw:02x}_f{floor}", floor, raw, {}, f"item 0x{raw:02x}")
        for raw in range(0x20, 0x3d) for floor in (5,)
    ] + [
        (f"item_{raw:02x}_f15", 15, raw, {}, f"item 0x{raw:02x}, area 2")
        for raw in range(0x20, 0x24)
    ] + [
        (f"item_{raw:02x}_hard", 5, raw, {159: 1}, f"item 0x{raw:02x}, option A")
        for raw in list(range(0x20, 0x24)) + [0x33, 0x38]
    ] + [
        ("item_33_owned", 5, 0x33, {32: 1, 15: 1}, "Iron Sword already owned"),
        ("item_38_owned", 5, 0x38, {37: 1, 16: 1}, "Iron Shield already owned"),
        ("item_3c_sacred_preset", 5, 0x3c, {41: 1}, "Sacred Shield flag preset"),
        ("item_2b_staff", 5, 0x2b, {24: 1}, "Warp Staff with charges left"),
        ("door_yellow_many", 5, 0x01, {8: 5}, "yellow door, five keys"),
        ("stairs_up_limit", 5, 0x0b, {5: 9}, "up stairs below the floor limit"),
        ("stairs_down_f1", 1, 0x0c, {5: 3}, "down stairs on floor 1"),
    ] + [
        (f"raw_{raw:02x}", 4, raw, {3: 100}, f"walk into raw 0x{raw:02x}")
        for raw in (0x04, 0x05, 0x0e, 0x0f, 0x10, 0x11, 0x08, 0x12)
    ] + [
        (f"monster_{raw:02x}", 5, raw, {3: 10}, f"monster raw 0x{raw:02x}")
        for raw in (0x3d, 0x3f, 0x41, 0x43, 0x45, 0x47, 0x4d, 0x4f, 0x51)
    ] + [
        ("monster_3d_weak", 5, 0x3d, {1: 15, 2: 5}, "weak hero vs slime"),
        ("monster_51_weak", 5, 0x51, {1: 10, 2: 10}, "hopeless fight"),
        ("monster_3d_hard", 5, 0x3d, {159: 1}, "slime, option A"),
    ]
] + [
    # Button1Click (0x44da10): the dialogue confirm, one witness per ported
    # case.  g_subevent_id is aux roujin_aux; 0x48c5c8 is ui_selector.
    Witness(f"button1click_{sub:02x}", "button1click", 0x44DA10, floor, 5, 10,
            extra_state=extra, aux_overrides={"roujin_aux": sub},
            note=note)
    for sub, floor, extra, note in [
        (0x00, 3, {17: 2}, "old-man confirm: Orb of the Hero 2 -> 1"),
        (0x0b, 2, {}, "opening wake-up text: resume Timer2"),
        (0x0d, 2, {0x0C: 1}, "end of the opening: clear state[0xc]"),
        (0x0e, 5, {}, "appends a 6-command monster-move timeline"),
        (0x0f, 5, {}, "appends an 8-command timeline"),
        (0x10, 35, {}, "writes two floor-35 tiles"),
        (0x11, 5, {}, "hide and stop"),
        (0x14, 3, {}, "Zeno speech confirm: openning2 (651 commands)"),
        (0x15, 3, {}, "'What? Oh!' confirm: resume Timer2"),
        (0x17, 10, {}, "Golden Knight speech confirm: resume Timer2"),
        (0x1a, 10, {159: 1}, "fairy confirm, option A = 1: sets 0x6a"),
        (0x3a, 20, {}, "high id: resume"),
        (0x47, 20, {}, "high id: hide and stop"),
    ]
] + [
    # taisen2 (0x47f7d4, 0xb/10): the original reads the sub-dispatch
    # command's arg2 (target index) from the timeline, so the native call
    # seeds command index 1 and leaves the count at 0.
    Witness("madoushi2", "madoushi2", 0x462560, 49, 5, 5,
            native_commands=[(0x0B, 3, 0)], native_count=0,
            note="splice 0x0b/3: eight Magic Sergeants around 0x1b; the 129th command is the stale slot its extra count++ exposes"),
    Witness("taisen2_win", "taisen2", 0x47F7D4, 40, 4, 4,
            tile=(40, 0x3F), target_index=40,
            native_commands=[(0x0B, 10, 40)], native_count=0,
            note="scripted battle, player survives"),
    Witness("taisen2_lose", "taisen2", 0x47F7D4, 40, 4, 4,
            tile=(40, 0x3F), target_index=40,
            extra_state={0: 5, 2: 5},
            native_commands=[(0x0B, 10, 40)], native_count=0,
            note="scripted battle, player dies; try again: Yes"),
    Witness("taisen2_lose_no", "taisen2", 0x47F7D4, 40, 4, 4,
            tile=(40, 0x3F), target_index=40,
            extra_state={0: 5, 2: 5},
            native_commands=[(0x0B, 10, 40)], native_count=0,
            message_dlg=7,
            note="scripted battle, player dies; try again: No"),
    Witness("taisen2_win3", "taisen2", 0x47F7D4, 40, 4, 4,
            tile=(40, 0x55), target_index=40,
            native_commands=[(0x0B, 10, 40)], native_count=0,
            note="scripted battle, three-turn win (Zombie)"),
    Witness("taisen2_lose4", "taisen2", 0x47F7D4, 40, 4, 4,
            tile=(40, 0x3F), target_index=40,
            extra_state={0: 20, 1: 10, 2: 15},
            native_commands=[(0x0B, 10, 40)], native_count=0,
            note="scripted battle, four-turn loss"),
    Witness("taisen2_master", "taisen2", 0x47F7D4, 40, 4, 4,
            tile=(40, 0x85), target_index=40,
            extra_state={1: 400, 2: 10000},
            native_commands=[(0x0B, 10, 40)], native_count=0,
            note="scripted battle, high monster range (index 20)"),
    Witness("taisen2_win_drain", "taisen2", 0x47F7D4, 10, 4, 4,
            tile=(40, 0x3F), target_index=40,
            native_commands=[(0x0B, 10, 40)], native_count=0,
            note="battle win on floor 10: moncheck is ported, so the drain runs"),
]


def state_ints(raw: bytes) -> list[int]:
    return [int.from_bytes(raw[i * 4:i * 4 + 4], "little", signed=True)
            for i in range(event_snapshot.STATE_INT_COUNT)]


def run_drain(exe: Path, w: Witness) -> dict | None:
    """Strict drain with dialogue confirms; None unless complete."""
    import run_execution_oracle as execution   # imports this module
    r = execution.run_case(exe, w, 6000, permissive_vcl=False,
                           complete_dialogs=True)
    d = r["drain"]
    if (d["stop"] not in ("drained", "modal_idle", "choice_pending", "timer_off")
            or d["tainted"]):
        return None
    return {
        "stop": d["stop"],
        "steps": d["steps"],
        "dialogues": [m["subevent"] for m in d["modal"]],
        "state_diff": d["state_diff"],
        "map_diff": d["map_diff"],
        "message_dlg": int(w.message_dlg),
    }


def run_witness(exe: Path, w: Witness, with_drain: bool = True) -> dict:
    snap = w.snapshot()
    state = list(snap.state)
    aux = snap.aux_with_derived()
    aux_in = {name: int(aux.get(name, native.AUX_DEFAULTS[name]))
              for name in native.AUX_ADDRS}
    harness = native.NativeHarness(exe, presentation_stubs=True)
    result = harness.call(w.entry, state, snap.map_block, w.floor, aux,
                          commands=w.native_commands or None,
                          count=w.native_count)
    final_state = state_ints(result.state)
    state_diff = [{"index": i, "before": a, "after": b}
                  for i, (a, b) in enumerate(zip(state, final_state)) if a != b]
    map_diff = [{"index": i, "before": a, "after": b}
                for i, (a, b) in enumerate(zip(snap.map_block, result.map_block))
                if a != b]
    aux_final = {name: int(result.aux.get(name, 0)) for name in native.AUX_ADDRS}
    aux_diff = {name: {"before": aux_in[name], "after": aux_final[name]}
                for name in native.AUX_ADDRS
                if (aux_in[name] & 0xFFFFFFFF) != aux_final[name]}
    return {
        "schema": SCHEMA,
        "binary_sha256": result.digest,
        "case_id": w.case_id,
        "handler": w.handler,
        "entry": f"0x{w.entry:08x}",
        "note": w.note,
        "snapshot": {
            "floor": w.floor,
            "state": state,
            "map_b64": base64.b64encode(snap.map_block).decode("ascii"),
            "aux": aux_in,
        },
        "builder": {
            "ok": result.ok,
            "termination": result.termination,
            "error": result.error,
            "command_words": [list(c) for c in result.commands],
            "state_diff": state_diff,
            "map_diff": map_diff,
            "aux_final": aux_final,
            "aux_diff": aux_diff,
        },
        "drain": run_drain(exe, w) if (with_drain and w.with_drain) else None,
        "executed": result.executed,
        "executed_runs": result.executed_runs,
    }


def to_vec(vector: dict) -> str:
    snap = vector["snapshot"]
    b = vector["builder"]
    lines = [f"case {vector['case_id']}",
             f"handler {vector['handler']}",
             f"entry {int(vector['entry'], 16)}"]
    for i, v in enumerate(snap["state"]):
        if v:
            lines.append(f"state {i} {v}")
    lines.append("map " + base64.b64decode(snap["map_b64"]).hex())
    for name, v in snap["aux"].items():
        lines.append(f"aux {name} {v}")
    for op, a1, a2 in b["command_words"]:
        lines.append(f"cmd {op} {a1} {a2}")
    for d in b["state_diff"]:
        lines.append(f"dstate {d['index']} {d['after']}")
    for d in b["map_diff"]:
        lines.append(f"dmap {d['index']} {d['after']}")
    for name, d in b["aux_diff"].items():
        after = d["after"]
        if after & 0x80000000:
            after -= 1 << 32
        lines.append(f"daux {name} {after}")
    dr = vector.get("drain")
    if dr:
        for sub in dr["dialogues"]:
            lines.append(f"drain_dialog {sub}")
        if dr.get("message_dlg", 6) != 6:
            lines.append(f"drain_message_dlg {dr['message_dlg']}")
        for d in dr["state_diff"]:
            lines.append(f"xstate {d['index']} {d['after']}")
        for d in dr["map_diff"]:
            lines.append(f"xmap {d['index']} {d['after']}")
        lines.append("drain_end")
    lines.append("end")
    return "\n".join(lines) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    ap.add_argument("-d", "--output-dir", type=Path, default=None)
    ap.add_argument("--case", action="append",
                    help="witness id (repeatable); default: all")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--no-drain", action="store_true",
                    help="skip the execution-oracle drain")
    args = ap.parse_args()

    if args.list:
        for w in WITNESSES:
            print(f"{w.case_id:24s} {w.handler:14s} 0x{w.entry:08x} {w.note}")
        return 0
    if native.Uc is None:
        print("error: unicorn is not installed in this interpreter",
              file=sys.stderr)
        return 2
    if not args.exe.exists():
        print(f"error: executable not found: {args.exe}", file=sys.stderr)
        return 2
    wanted = set(args.case or [w.case_id for w in WITNESSES])
    unknown = wanted - {w.case_id for w in WITNESSES}
    if unknown:
        print(f"error: unknown witness: {', '.join(sorted(unknown))}",
              file=sys.stderr)
        return 2
    out = args.output_dir or default_out_dir()
    out.mkdir(parents=True, exist_ok=True)
    manifest = []
    failed = 0
    for w in WITNESSES:
        if w.case_id not in wanted:
            continue
        vector = run_witness(args.exe, w, not args.no_drain)
        b = vector["builder"]
        (out / f"{w.case_id}.json").write_text(
            json.dumps(vector, indent=1, sort_keys=True) + "\n",
            encoding="utf-8")
        if b["ok"]:
            (out / f"{w.case_id}.vec").write_text(to_vec(vector),
                                                  encoding="utf-8")
        else:
            failed += 1
        manifest.append({"case_id": w.case_id, "handler": w.handler,
                         "ok": b["ok"], "commands": len(b["command_words"]),
                         "state_changes": len(b["state_diff"]),
                         "map_changes": len(b["map_diff"])})
        print(f"{w.case_id}: ok={b['ok']} commands={len(b['command_words'])} "
              f"state={len(b['state_diff'])} map={len(b['map_diff'])} "
              f"{b['termination']}"
              f"{' drain=' + str(len(vector['drain']['dialogues'])) + ' dialogues' if vector.get('drain') else ' drain=none'}")
    (out / "manifest.json").write_text(
        json.dumps({"schema": SCHEMA, "vectors": manifest}, indent=1) + "\n",
        encoding="utf-8")
    print(f"wrote {len(manifest)} vector(s) to {out}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
