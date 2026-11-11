#!/usr/bin/env python3
"""Deterministic synthetic oracle over a lifted builder-IR bundle.

This runs the recovered command timeline in the reference VM
(`event_vm.EventVM`) from an `event_snapshot.Snapshot`.  It walks the builder
CFG from an optional entry block, applies path-filtered builder `write_global`
effects (state and `0x48c...` aux selectors), then executes the command list.
It records state/map deltas, presentation calls and unresolved builder
effects/edges.  It is a synthetic/executable-timeline oracle, not the native
original-code oracle.

Usage:
    run_synthetic_oracle.py builder_ir.json snapshot.json --entry-block 0x46cbc7 -o trace.json
    run_synthetic_oracle.py --compare trace_a.json trace_b.json
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

import event_snapshot
import event_vm


def load_ir(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def parse_immediate(text: str) -> int | None:
    if "," not in text:
        return None
    token = text.rsplit(",", 1)[1].strip()
    if token.startswith("0x"):
        return int(token, 16)
    try:
        return int(token, 10)
    except ValueError:
        return None


def s32(value: int) -> int:
    value &= 0xFFFFFFFF
    return value - 0x100000000 if value >= 0x80000000 else value


def apply_write(op: dict, state: list[int], aux: dict[str, int]) -> dict | None:
    instruction = str(op.get("instruction", ""))
    parts = instruction.split(" ", 1)
    if not parts or parts[0] != "mov" or "DWORD PTR" not in instruction.upper():
        return None
    target = str(op.get("target", ""))
    if not target.startswith("ds:0x"):
        return None
    try:
        addr = int(target.split(":", 1)[1], 16)
    except ValueError:
        return None
    value = parse_immediate(instruction)
    if value is None:
        return None
    value = s32(value)
    if 0x4B8688 <= addr < 0x4B8688 + event_snapshot.STATE_INT_COUNT * 4:
        index = (addr - 0x4B8688) // 4
        before = state[index]
        state[index] = value
        return {"kind": "state", "index": index, "before": before,
                "after": value, "address": f"0x{addr:08x}"}
    name = event_snapshot.AUX_ADDRS.get(addr)
    if name:
        before = aux.get(name, 0)
        aux[name] = value
        return {"kind": "aux", "name": name, "before": before,
                "after": value, "address": f"0x{addr:08x}"}
    return None


def parse_successors(text: str) -> list[tuple[int, str]]:
    out = []
    for part in str(text).split(" | "):
        m = re.match(r"^0x([0-9a-f]+):(.*)$", part.strip())
        if m:
            out.append((int(m.group(1), 16), m.group(2)))
    return out


def value_of(token: str, state: list[int], aux: dict[str, int]) -> int | None:
    token = token.strip()
    m = re.match(r"^(?:DWORD|WORD|BYTE) PTR ds:0x([0-9a-f]+)$", token, re.IGNORECASE)
    if m:
        addr = int(m.group(1), 16)
        if 0x4B8688 <= addr < 0x4B8688 + event_snapshot.STATE_INT_COUNT * 4:
            return state[(addr - 0x4B8688) // 4]
        name = event_snapshot.AUX_ADDRS.get(addr)
        return aux.get(name) if name else None
    if token.startswith("0x"):
        return int(token, 16)
    try:
        return int(token, 10)
    except ValueError:
        return None


def eval_predicate(expr: str, state: list[int], aux: dict[str, int]) -> bool | None:
    m = re.match(r"^(.*?)\s*(==|!=|>=|<=|>|<)\s*(.*?)$", expr.strip())
    if not m:
        return None
    left = value_of(m.group(1), state, aux)
    right = value_of(m.group(3), state, aux)
    if left is None or right is None:
        return None
    return {
        "==": left == right, "!=": left != right,
        ">": left > right, ">=": left >= right,
        "<": left < right, "<=": left <= right,
    }[m.group(2)]


def edge_truth(label: str, state: list[int], aux: dict[str, int]) -> bool | None:
    if label.startswith("branch:(") and label.endswith(")"):
        return eval_predicate(label[len("branch:("):-1], state, aux)
    if label.startswith("fallthrough:not(") and label.endswith(")"):
        inner = eval_predicate(label[len("fallthrough:not("):-1], state, aux)
        return None if inner is None else not inner
    if label.startswith("jump:") or label == "fallthrough":
        return True
    return None


def walk_builder(ir: dict, entry_block: int, state: list[int],
                 aux: dict[str, int]):
    blocks = {int(b["block_start"], 16): b for b in ir.get("blocks", [])}
    ops_by_block: dict[int, list[dict]] = {}
    for op in ir.get("ops", []):
        ops_by_block.setdefault(int(op["block_start"], 16), []).append(op)
    for items in ops_by_block.values():
        items.sort(key=lambda o: int(o["address"], 16))
    queue = [entry_block]
    visited: list[int] = []
    seen = set()
    writes = []
    unresolved: list[dict] = []
    calls: list[dict] = []
    external: list[dict] = []
    block_min = min(blocks) if blocks else 0
    block_max = max(blocks) if blocks else 0
    while queue:
        block = queue.pop(0)
        if block in seen:
            continue
        seen.add(block)
        if block not in blocks:
            if block < block_min or block > block_max:
                external.append({"kind": "external_exit",
                                 "block": f"0x{block:08x}"})
            else:
                unresolved.append({"kind": "missing_block",
                                   "block": f"0x{block:08x}"})
            continue
        visited.append(block)
        for op in ops_by_block.get(block, []):
            kind = op.get("kind")
            if kind == "write_global":
                applied = apply_write(op, state, aux)
                if applied is None:
                    unresolved.append({"kind": "unsupported_write", **op})
                else:
                    writes.append(applied)
            elif kind == "call":
                calls.append(op)
            elif kind == "write_mem":
                unresolved.append({"kind": "unresolved_map_write", **op})
        for part in str(blocks[block].get("successors", "")).split(" | "):
            part = part.strip()
            if part == "unresolved-switch":
                unresolved.append({"kind": "unresolved_switch",
                                   "block": f"0x{block:08x}"})
                continue
            m = re.match(r"^0x([0-9a-f]+):(.*)$", part)
            if not m:
                continue
            target = int(m.group(1), 16)
            label = m.group(2)
            if target not in blocks:
                if target < block_min or target > block_max:
                    external.append({"kind": "external_exit",
                                     "block": f"0x{block:08x}",
                                     "target": f"0x{target:08x}",
                                     "label": label})
                else:
                    unresolved.append({"kind": "missing_target",
                                       "block": f"0x{block:08x}",
                                       "target": f"0x{target:08x}", "label": label})
                continue
            truth = edge_truth(label, state, aux)
            if truth is False:
                continue
            if truth is None:
                # Do not merge unknown paths into one concrete state.
                unresolved.append({"kind": "unknown_edge",
                                   "block": f"0x{block:08x}",
                                   "target": f"0x{target:08x}", "label": label})
                continue
            queue.append(target)
    return writes, unresolved, calls, external, visited


def run(ir: dict, snapshot_manifest: dict, entry_block: int | None = None) -> dict:
    snapshot = event_snapshot.Snapshot.from_manifest(snapshot_manifest)
    floor = snapshot.state[0x010 // 4]
    vm = event_vm.EventVM(state=list(snapshot.state))
    for index, tile in enumerate(snapshot.floor_tiles(floor)):
        vm.map_tiles[(floor, index)] = tile
    aux = snapshot.aux_with_derived()
    if entry_block is None:
        starts = [int(b["block_start"], 16) for b in ir.get("blocks", [])]
        entry_block = min(starts) if starts else 0
    builder_writes, builder_unresolved, builder_calls, builder_external, visited = walk_builder(
        ir, entry_block, vm.state, aux)
    visited_set = set(visited)

    vm.current_sprite = aux.get("sprite", 0)
    vm.monster_index = aux.get("current_monster", 0)
    vm.monster_hp = aux.get("monster_hp", 0)
    for f in range(1, 51):
        for index, tile in enumerate(snapshot.floor_tiles(f)):
            vm.map_tiles[(f, index)] = tile

    commands = []
    command_words = []
    unresolved_appends = []
    for entry in ir.get("appends", []):
        block = int(str(entry.get("block_start", "0x0")), 16)
        if block not in visited_set:
            continue
        opcode = entry.get("opcode")
        if opcode is None:
            unresolved_appends.append({"reason": "missing_opcode", **entry})
            continue
        arg1 = entry.get("arg1")
        arg2 = entry.get("arg2")
        if arg1 is None or arg2 is None:
            unresolved_appends.append({
                "reason": "unresolved_argument", "arg1": arg1, "arg2": arg2,
                **entry,
            })
            continue
        commands.append(event_vm.Command(opcode=opcode, arg1=arg1, arg2=arg2))
        command_words.append([opcode, arg1, arg2])
    vm.load(commands)
    initial_state = list(vm.state)
    initial_map = dict(vm.map_tiles)
    steps = vm.run(max_steps=200000)
    state_diff = [
        {"index": i, "before": initial_state[i], "after": vm.state[i]}
        for i in range(len(vm.state)) if initial_state[i] != vm.state[i]
    ]
    map_diff = [
        {"floor": f, "index": idx, "before": initial_map.get((f, idx)),
         "after": value}
        for (f, idx), value in sorted(vm.map_tiles.items())
        if initial_map.get((f, idx)) != value
    ]
    aux_final = dict(aux)
    aux_final.update({
        "floor": vm.state[0x010 // 4],
        "x": vm.state[0x018 // 4],
        "y": vm.state[0x01C // 4],
        "life": vm.state[0],
        "attack": vm.state[1],
        "defense": vm.state[2],
    })
    for name, index in event_snapshot.STATE_VIEWS.items():
        aux_final[name] = vm.state[index]
    complete = not unresolved_appends and not builder_unresolved
    snapshot_digest = __import__("hashlib").sha256(
        json.dumps(snapshot_manifest, sort_keys=True).encode("utf-8")).hexdigest()
    return {
        "schema_version": 1,
        "method": ir.get("method"),
        "floor": floor,
        "entry_block": f"0x{entry_block:08x}",
        "visited_blocks": [f"0x{b:08x}" for b in visited],
        "commands": len(commands),
        "command_words": command_words,
        "unresolved_appends": unresolved_appends,
        "unresolved_args": len([u for u in unresolved_appends
                                if u.get("reason") == "unresolved_argument"]),
        "steps": steps,
        "gameover": vm.gameover,
        "complete": complete,
        "snapshot_digest": snapshot_digest,
        "builder_writes": builder_writes,
        "builder_calls": builder_calls,
        "builder_external": builder_external,
        "builder_unresolved": builder_unresolved,
        "aux_final": aux_final,
        "state_diff": state_diff,
        "map_diff": map_diff,
        "log": vm.log,
    }


def compare(a: dict, b: dict) -> bool:
    keys = ("method", "floor", "entry_block", "visited_blocks", "commands",
            "command_words", "unresolved_appends", "unresolved_args", "steps",
            "gameover", "complete", "snapshot_digest", "builder_writes",
            "builder_calls", "builder_external", "builder_unresolved", "aux_final", "state_diff",
            "map_diff", "log")
    return all(a.get(k) == b.get(k) for k in keys)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ir", nargs="?", type=Path)
    ap.add_argument("snapshot", nargs="?", type=Path)
    ap.add_argument("-o", "--output", type=Path)
    ap.add_argument("--entry-block", type=lambda x: int(x, 16), default=None,
                    help="hex block start; defaults to the lowest block")
    ap.add_argument("--compare", nargs=2, type=Path,
                    metavar=("TRACE_A", "TRACE_B"))
    args = ap.parse_args()

    if args.compare:
        a = json.loads(args.compare[0].read_text(encoding="utf-8"))
        b = json.loads(args.compare[1].read_text(encoding="utf-8"))
        ok = compare(a, b)
        print("traces match" if ok else "traces differ")
        return 0 if ok else 1

    if not args.ir or not args.snapshot:
        ap.error("ir and snapshot are required unless --compare is used")
    trace = run(load_ir(args.ir),
                json.loads(args.snapshot.read_text(encoding="utf-8")),
                args.entry_block)
    if args.output:
        args.output.write_text(json.dumps(trace, indent=2) + "\n",
                               encoding="utf-8")
        print(f"wrote {args.output}")
    else:
        print(json.dumps(trace, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
