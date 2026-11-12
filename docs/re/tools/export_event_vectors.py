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
    drain           execution-oracle result (null until P3b drain runs)
    executed        sorted executed instruction addresses

``.vec`` format, one record per line (all numbers decimal)::

    case <id>          handler <name>        entry <addr>
    state <i> <v>      (nonzero initial state ints)
    map <hex>          (6273-byte initial map block as hex)
    aux <name> <v>
    cmd <op> <a1> <a2> (builder output in append order)
    dstate <i> <v>     (final value of every changed state int)
    dmap <off> <v>     (final value of every changed map byte)
    daux <name> <v>    (final value of every changed auxiliary)
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
    target_index: int = 0
    sound_state: int = 0
    extra_state: dict[int, int] = field(default_factory=dict)
    aux_overrides: dict[str, int] = field(default_factory=dict)
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
        if self.tile is not None:
            index, value = self.tile
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
        (0x15, 3, {}, "'What? Oh!' confirm: resume Timer2"),
        (0x17, 10, {}, "Golden Knight speech confirm: resume Timer2"),
        (0x1a, 10, {159: 1}, "fairy confirm, option A = 1: sets 0x6a"),
        (0x3a, 20, {}, "high id: resume"),
        (0x47, 20, {}, "high id: hide and stop"),
    ]
]


def state_ints(raw: bytes) -> list[int]:
    return [int.from_bytes(raw[i * 4:i * 4 + 4], "little", signed=True)
            for i in range(event_snapshot.STATE_INT_COUNT)]


def run_witness(exe: Path, w: Witness) -> dict:
    snap = w.snapshot()
    state = list(snap.state)
    aux = snap.aux_with_derived()
    aux_in = {name: int(aux.get(name, native.AUX_DEFAULTS[name]))
              for name in native.AUX_ADDRS}
    harness = native.NativeHarness(exe, presentation_stubs=True)
    result = harness.call(w.entry, state, snap.map_block, w.floor, aux)
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
        "drain": None,
        "executed": result.executed,
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
        vector = run_witness(args.exe, w)
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
              f"{b['termination']}")
    (out / "manifest.json").write_text(
        json.dumps({"schema": SCHEMA, "vectors": manifest}, indent=1) + "\n",
        encoding="utf-8")
    print(f"wrote {len(manifest)} vector(s) to {out}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
