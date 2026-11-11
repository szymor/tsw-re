#!/usr/bin/env python3
"""Decode event switch tables and inventory event call sites.

This is the P2 trigger-graph extractor.  It reads the pinned executable,
decodes the known floor/raw-tile switch tables, follows direct call sites into
the event entry points, and writes local TSV drafts for:

* switch-table entries and their resolved handlers;
* event call sites;
* a first-pass trigger inventory.

Generated data stays outside the repository by default.

Usage:
    python3 docs/re/tools/scan_event_triggers.py /path/to/TSW.exe
    python3 docs/re/tools/scan_event_triggers.py /path/to/TSW.exe --summary
    python3 docs/re/tools/scan_event_triggers.py /path/to/TSW.exe -o /tmp/opencode
"""

from __future__ import annotations

import argparse
import collections
import os
import re
import sys
from pathlib import Path

import scan_event_buffers as buffers

EXPECTED_SHA256 = buffers.EXPECTED_SHA256

# Known switch tables.  ``length`` is the number of entries; ``jump`` is the
# pointer table for byte-indexed switches (or the table itself for a direct
# pointer switch).
TABLES = [
    {
        "id": "handan_raw_00_18",
        "kind": "pointer",
        "base": 0x4444E3,
        "jump": 0x4444E3,
        "length": 25,
        "index_base": 0x00,
        "index_name": "raw_tile",
        "source": "handan",
        "notes": "raw tiles 0x00..0x18",
    },
    {
        "id": "handan_raw_20_32",
        "kind": "pointer",
        "base": 0x445698,
        "jump": 0x445698,
        "length": 19,
        "index_base": 0x20,
        "index_name": "raw_tile",
        "source": "handan",
        "notes": "raw tiles 0x20..0x32; index is raw-0x20",
    },
    {
        "id": "handan_raw_33_3c",
        "kind": "pointer",
        "base": 0x445C91,
        "jump": 0x445C91,
        "length": 10,
        "index_base": 0x33,
        "index_name": "raw_tile",
        "source": "handan",
        "notes": "raw tiles 0x33..0x3c; index is raw-0x33",
    },
    {
        "id": "mevent_floor",
        "kind": "byte",
        "base": 0x4639B6,
        "jump": 0x4639E8,
        "jump_length": 17,
        "length": 50,
        "index_base": 0,
        "index_name": "floor",
        "source": "mevent",
        "notes": "floors 0..49",
    },
    {
        "id": "ichicheck_floor",
        "kind": "byte",
        "base": 0x46CB63,
        "jump": 0x46CB8B,
        "jump_length": 15,
        "length": 40,
        "index_base": 10,
        "index_name": "floor",
        "source": "ichicheck",
        "notes": "floors 10..49; index is floor-10",
    },
    {
        "id": "roujin_floor",
        "kind": "byte",
        "base": 0x449767,
        "jump": 0x449798,
        "jump_length": 19,
        "length": 49,
        "index_base": 0,
        "index_name": "floor",
        "source": "roujin",
        "notes": "floors 0..48",
    },
    {
        "id": "moncheck_floor",
        "kind": "byte",
        "base": 0x466512,
        "jump": 0x466545,
        "jump_length": 23,
        "length": 51,
        "index_base": 0,
        "index_name": "floor",
        "source": "moncheck",
        "notes": "floors 0..50",
    },
]

# Event entry points worth following from call sites.
ENTRY_POINTS = [
    "formkeydown", "timer3ontimer", "handan", "mevent", "ichicheck",
    "Timer1Timer", "timer2timer", "stackwork", "syounin", "zyouout",
    "roujin", "touzoku", "madoushi1", "madoushi2", "banpaiawork", "yousei",
    "openning31", "openning2", "openning9", "zenolivework", "moncheck",
    "taisen2", "gameover", "takowork1", "takowork2",
]

MANUAL_TRIGGERS = [
    # id, floor, map_index, raw_tile, entry_point, caller_addr, handler,
    # condition, one_shot, notes
    ("timer1-floor15-secret", 15, 0x3C, "*", "Timer1Timer", "0x443120",
     "0x454a58", "floor==15 and map[0x3c]==6 and flag[0x4b88e8]<2", "counter", "takowork2"),
    ("timer1-floor35-secret", 35, 0x47, "*", "Timer1Timer", "0x443120",
     "0x4548a0", "floor==35 and map[0x47]==6 and flag[0x4b88ec]<2", "counter", "takowork1"),
    ("handan-keys", "*", "*", "0x1d..0x1f", "handan", "0x444490",
     "0x4454fb", "raw in 0x1d..0x1f", "-", "yellow/red/blue key pickup"),
    ("handan-oldman-extra", "*", "*", "0x19..0x1a", "handan", "0x444490",
     "0x44538a", "raw in 0x19..0x1a", "-", "roujin call path"),
    ("handan-fairy", "*", "*", "0x17..0x18", "handan", "0x444490",
     "0x44537d", "raw in 0x17..0x18", "-", "yousei/fairy call path"),
    ("floor3-raw07-opening", 3, "*", "0x07", "handan/mevent", "0x4447d2",
     "0x463a2c", "floor==3 and raw==0x07", "?", "clears tile, appends 0x0a sub-event, calls zenolivework"),
    ("handan-guarded", "*", "*", "0x1b..0x1c", "handan", "0x444490",
     "0x445397", "raw in 0x1b..0x1c", "-", "guarded map-event branch"),
    ("handan-monsters", "*", "*", "0x3d..0x5a", "handan", "0x444490",
     "battle path", "raw in 0x3d..0x5a", "-", "normal monster encounter"),
    ("handan-event-markers", "*", "*", "0x5b..0x5d,0x61..0x69,0x73..0x7b,0x85..0x9d",
     "handan", "0x444490", "event/mevent/ichicheck", "raw is an event marker", "-",
     "owner depends on floor/index; see mevent/ichicheck"),
]

TARGET_LABELS = {
    0x448FBC: "handan.return",
    0x4445D3: "handan.door",
    0x4447C5: "handan.move",
    0x4447D2: "handan.case7_mevent",
    0x4447DF: "handan.case9_secret_wall",
    0x4448CE: "handan.case10_fixed_event",
    0x444CD8: "handan.stairs",
    0x44509B: "handan.raw10_event",
    0x445363: "syounin",
    0x445370: "touzoku",
    0x44537D: "yousei",
    0x44538A: "roujin",
    0x445397: "handan.guarded_branch",
    0x4454FB: "handan.key_pickup",
    0x44566E: "handan.consumable_switch",
    0x445C67: "handan.equipment_switch",
}


def sha256(path: Path) -> str:
    return buffers.sha256(path)


def load_byte_table(data: bytes, base: int, length: int) -> list[int]:
    off = base - 0x401000 + 0x400
    values = list(data[off:off + length])
    if len(values) != length:
        raise ValueError(f"short byte table at 0x{base:08x}")
    return values


def decode_pointer_entries(data: bytes, base: int, length: int) -> list[int]:
    out: list[int] = []
    for i in range(length):
        value = buffers._read_u32(data, base + i * 4)
        if value is None or not (0x401000 <= value < 0x488E94):
            raise ValueError(f"invalid pointer at 0x{base + i * 4:08x}")
        out.append(value)
    return out


def resolve_byte_targets(values: list[int], jump: list[int]) -> list[int]:
    out: list[int] = []
    for value in values:
        if value >= len(jump):
            raise ValueError(f"byte switch index {value} exceeds jump table {len(jump)}")
        out.append(jump[value])
    return out


def method_labels(methods: list[tuple[int, str]]) -> list[tuple[int, str]]:
    return sorted(methods + buffers.EVENT_HANDLERS)


def label_for(methods: list[tuple[int, str]], addr: int) -> str:
    if addr in TARGET_LABELS:
        return TARGET_LABELS[addr]
    return buffers.nearest_method(methods, addr)


def decode_tables(exe: Path):
    data = exe.read_bytes()
    rows: list[dict[str, object]] = []
    data_ranges: list[tuple[int, int]] = []
    for spec in TABLES:
        if spec["kind"] == "pointer":
            base = int(spec["base"])
            length = int(spec["length"])
            targets = decode_pointer_entries(data, base, length)
            data_ranges.append((base, base + 4 * length))
        else:
            base = int(spec["base"])
            jump_base = int(spec["jump"])
            length = int(spec["length"])
            jump_length = int(spec["jump_length"])
            values = load_byte_table(data, base, length)
            jump = decode_pointer_entries(data, jump_base, jump_length)
            targets = resolve_byte_targets(values, jump)
            data_ranges.append((base, base + length))
            data_ranges.append((jump_base, jump_base + 4 * jump_length))
        for index, target in enumerate(targets):
            rows.append({
                "table_id": spec["id"],
                "kind": spec["kind"],
                "base": int(spec["base"]),
                "jump": int(spec["jump"]),
                "index": int(spec["index_base"]) + index,
                "target": target,
                "source": spec["source"],
                "notes": spec["notes"],
            })
    return rows, data_ranges


def in_ranges(addr: int, ranges: list[tuple[int, int]]) -> bool:
    return any(lo <= addr < hi for lo, hi in ranges)


def scan_calls(exe: Path, methods: list[tuple[int, str]],
               entry_addrs: dict[int, str], extra_entries: set[int] | None = None,
               data_ranges: list[tuple[int, int]] | None = None) -> list[dict[str, object]]:
    extra_entries = extra_entries or set()
    data_ranges = data_ranges or []
    entry = ({addr for addr, _ in methods} | set(entry_addrs) | extra_entries)
    insns = buffers.disassemble(exe, entry)
    rows: list[dict[str, object]] = []
    for addr, text in insns:
        if in_ranges(addr, data_ranges):
            continue
        m = re.match(r"^(call|jmp)\s+0x([0-9a-f]+)$", text)
        if not m:
            continue
        target = int(m.group(2), 16)
        if target not in entry_addrs:
            continue
        caller = buffers.nearest_method(methods, addr)
        rows.append({
            "caller": caller,
            "call_addr": addr,
            "kind": m.group(1),
            "callee": entry_addrs[target],
            "callee_addr": target,
            "instruction": text,
        })
    return rows


def trigger_rows(tables: list[dict[str, object]],
                 methods: list[tuple[int, str]]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for row in tables:
        table_id = str(row["table_id"])
        index = int(row["index"])
        target = row["target"]
        target_label = label_for(methods, int(target)) if target is not None else "none"
        if table_id == "ichicheck_floor":
            event_index = index  # index is already floor (index_base=10)
            floor = event_index
            condition = f"floor == {floor}"
            raw = "*"
            entry = "ichicheck"
            caller = "0x46cb1c"
        elif table_id == "mevent_floor":
            floor = index
            condition = f"floor == {floor} and mevent path"
            raw = "*"
            entry = "mevent"
            caller = "0x46397c"
        elif table_id == "roujin_floor":
            floor = index
            condition = f"floor == {floor} and old-man contact"
            raw = "*"
            entry = "roujin"
            caller = "0x449708"
        elif table_id == "moncheck_floor":
            floor = index
            condition = f"floor == {floor} and monster-check path"
            raw = "*"
            entry = "moncheck"
            caller = "0x4664e4"
        else:
            floor = "*"
            raw = f"0x{index:02x}"
            condition = f"raw_tile == 0x{index:02x}"
            entry = "handan"
            caller = "0x444490"
        rows.append({
            "trigger_id": f"{table_id}:{index}",
            "floor": floor,
            "map_index": "*",
            "raw_tile": raw,
            "entry_point": entry,
            "caller_addr": caller,
            "handler": f"0x{int(target):08x}" if target is not None else "none",
            "handler_label": target_label,
            "condition": condition,
            "one_shot_flag": "?",
            "notes": row["notes"],
        })
    for item in MANUAL_TRIGGERS:
        trigger_id, floor, map_index, raw, entry, caller, handler, condition, one_shot, notes = item
        rows.append({
            "trigger_id": trigger_id, "floor": floor, "map_index": map_index,
            "raw_tile": raw, "entry_point": entry, "caller_addr": caller,
            "handler": handler, "handler_label": "", "condition": condition,
            "one_shot_flag": one_shot, "notes": notes,
        })
    return rows


def write_tables(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        f.write("table_id\tkind\tbase\tjump\tindex\ttarget\tsource\tnotes\n")
        for r in rows:
            target = f"0x{int(r['target']):08x}" if r["target"] is not None else "none"
            f.write(f"{r['table_id']}\t{r['kind']}\t0x{int(r['base']):08x}\t"
                    f"0x{int(r['jump']):08x}\t{r['index']}\t{target}\t"
                    f"{r['source']}\t{r['notes']}\n")


def write_calls(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        f.write("caller\tcall_addr\tkind\tcallee\tcallee_addr\tinstruction\n")
        for r in rows:
            f.write(f"{r['caller']}\t0x{int(r['call_addr']):08x}\t{r['kind']}\t"
                    f"{r['callee']}\t0x{int(r['callee_addr']):08x}\t{r['instruction']}\n")


def write_triggers(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        f.write("trigger_id\tfloor\tmap_index\traw_tile\tentry_point\tcaller_addr\t"
                "handler\thandler_label\tcondition\tone_shot_flag\tnotes\n")
        for r in rows:
            f.write("\t".join(str(r[k]) for k in (
                "trigger_id", "floor", "map_index", "raw_tile", "entry_point",
                "caller_addr", "handler", "handler_label", "condition",
                "one_shot_flag", "notes")) + "\n")


def write_manifest(path: Path, digest: str, pinned: bool, table_count: int,
                   call_count: int, trigger_count: int) -> None:
    path.write_text(
        "extractor\tscan_event_triggers.py\n"
        f"sha256\t{digest}\n"
        f"pinned\t{'yes' if pinned else 'no'}\n"
        f"tables\t{table_count}\n"
        f"calls\t{call_count}\n"
        f"triggers\t{trigger_count}\n",
        encoding="utf-8",
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("exe", type=Path)
    ap.add_argument("-o", "--output", type=Path)
    ap.add_argument("--summary", action="store_true")
    ap.add_argument("--allow-other", action="store_true")
    args = ap.parse_args()

    if not args.exe.exists():
        print(f"error: {args.exe} does not exist", file=sys.stderr)
        return 2
    digest = sha256(args.exe)
    if digest != EXPECTED_SHA256:
        if not args.allow_other:
            print(f"error: binary hash {digest} does not match {EXPECTED_SHA256}",
                  file=sys.stderr)
            return 3
        print("warning: scanning an unpinned binary", file=sys.stderr)

    true_methods = buffers.load_methods(
        Path(__file__).resolve().parents[1] / "method-table.tsv")
    methods = method_labels(true_methods)
    entries = {addr: name for addr, name in true_methods if name in ENTRY_POINTS}
    tables, table_ranges = decode_tables(args.exe)
    table_targets = {int(r["target"]) for r in tables}
    calls = scan_calls(args.exe, methods, entries, table_targets,
                       table_ranges + buffers.KNOWN_DATA_RANGES)
    triggers = trigger_rows(tables, methods)

    cache = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "tsw"
    outdir = args.output or cache
    write_tables(outdir / "event_switch_tables.tsv", tables)
    write_calls(outdir / "event_call_sites.tsv", calls)
    write_triggers(outdir / "event_triggers.tsv", triggers)
    write_manifest(outdir / "event_trigger_manifest.txt", digest,
                   digest == EXPECTED_SHA256, len(tables), len(calls), len(triggers))

    if args.summary:
        by_source = collections.Counter(str(r["source"]) for r in tables)
        print(f"sha256: {digest}")
        print(f"switch entries: {len(tables)}")
        for source, count in sorted(by_source.items()):
            print(f"  {source}: {count}")
        print(f"call sites: {len(calls)}")
        by_callee = collections.Counter(str(r["callee"]) for r in calls)
        for callee, count in by_callee.most_common(20):
            print(f"  -> {callee}: {count}")
        print(f"trigger rows: {len(triggers)}")
        return 0

    print(f"wrote {outdir / 'event_switch_tables.tsv'}")
    print(f"wrote {outdir / 'event_call_sites.tsv'}")
    print(f"wrote {outdir / 'event_triggers.tsv'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
