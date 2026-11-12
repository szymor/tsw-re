#!/usr/bin/env python3
"""Compose a static event timeline from a builder-IR bundle and callees.

This is the trace-free composer used for P3.  It walks the builder CFG in the
same way as the synthetic oracle, but interleaves append rows and builder calls
by instruction address and inlines known builder callees.  A callee contract can
consume the last command (as ``doorstackin`` does) and supply the consumed
arguments to the callee's unresolved append rows.

The native oracle is used only to validate the resulting static command stream;
it is not an input to composition.

Usage:
    compose_event_timeline.py MAIN_IR.json --callee-dir CALLEE_DIR \
        --snapshot snapshot.json --entry-block 0x46cbc7 -o timeline.json
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

import event_snapshot
import native_event_oracle as native
import run_synthetic_oracle as synthetic

CALL_CONTRACTS = {
    0x4660D0: {  # doorstackin
        "name": "doorstackin",
        "consumes_last": 1,
        "last_arg1_to": "arg1",
        "last_arg2_to": "arg2",
        "aux_from_last": {"event_selector": 2},
        "resolved_writes": {"ds:0x48c55c"},
    },
    0x449E18: {"name": "zyouout"},
    0x461EF4: {"name": "madoushilivework"},
    0x46397C: {"name": "mevent"},
    0x461A0C: {"name": "zenolivework"},
}


# Blocks whose successor choice needs a static path contract rather than the
# generic compare matcher.
SWITCH_CONTRACTS = {
    0x44975A: {  # roujin floor switch
        "index_state": 0x010 // 4,
        "byte_table": 0x449767,
        "pointer_table": 0x449798,
        "default": 0x449B6A,
    },
    0x4639A9: {  # mevent floor switch
        "index_state": 0x010 // 4,
        "byte_table": 0x4639B6,
        "pointer_table": 0x4639E8,
        "default": 0x464024,
    },
}

PATH_CONTRACTS = {
    0x449708: {"kind": "floor_max", "max": 0x30,
               "true": 0x449B6A, "false": 0x44975A},
    0x449C8A: {"kind": "state_selector", "selector": "event_selector",
               "zero": 0x449CAD, "one": 0x449DE3, "two": 0x449DF6,
               "other": 0x449E07},
    0x449CAD: {"kind": "state_value", "index": 0x048 // 4,
               "zero": 0x449CBB, "one": 0x449CCD, "other": 0x449CDD},
    0x449CDD: {"kind": "floor_equal", "value": 3,
               "true": 0x449CF2, "false": 0x449E07},
    0x449B6A: {"kind": "aux_equal", "name": "roujin_selector", "value": 1,
               "true": 0x449E07, "false": 0x449B77},
    0x44A0CB: {"kind": "byte_global", "address": 0x489BA4,
               "zero": 0x44A0F4, "one": 0x44A142, "other": 0x44A18E},
    0x44A18E: {"kind": "aux_ge", "name": "roujin_aux", "value": 0x0A,
               "true": 0x44A219, "false": 0x44A1E3},
    0x44A395: {"kind": "aux_not_equal", "name": "ui_selector", "value": 0x5D,
               "true": 0x44A3DE, "false": 0x44A39F},
    0x44A3DE: {"kind": "dword_global_ge", "address": 0x4B8924,
               "value": 1, "true": 0x44A419, "false": 0x44A3F8},
    0x464048: {"kind": "form_width", "width": 32, "first": 0x20,
               "second": 0x28, "zero": 0x46410D, "one": 0x464122,
               "other": 0x464135},
    0x464135: {"kind": "local_ge", "value": 0x28, "threshold": 0,
               "true": 0x464141, "false": 0x4642E5},
    0x444490: {"kind": "tile_switch", "floor_state": 0x010 // 4,
               "target_aux": "target_index", "high": 0x19,
               "max": 0x18, "high_target": 0x444547,
               "default": 0x448FBC, "pointer_table": 0x4444E3},
    0x46397C: {"kind": "floor_max", "max": 0x31,
               "true": 0x464024, "false": 0x4639A9},
    0x4642E5: {"kind": "facing_selector", "index": 0x160 // 4,
               "one": 0x4651A1, "two": 0x4651AF, "three": 0x4651BD,
               "four": 0x4651CB, "other": 0x4651D7},
    0x4651D7: {"kind": "facing_selector", "index": 0x160 // 4,
               "one": 0x465203, "two": 0x465211, "three": 0x46521F,
               "four": 0x46522D, "other": 0x465239},
}

LOOP_CONTRACTS = {
    0x464141: {
        "counter_initial": 0x28,
        "counter_final": 0,
        "step": -1,
        "arg2_base": 0x64,
        "arg2_offsets": [5, 4, 3, 2, 1, 0],
    },
}

BLOCK_EFFECTS = {
    0x449805: {
        "stat_indices": [1, 2],
        "percent": 3,
        "resolved_targets": {"ds:0x4b868c", "ds:0x4b8690"},
    },
    0x463A2C: {
        "map_tile": {"floor_state": 0x010 // 4,
                     "target_aux": "target_index", "value": 0x06},
        "resolved_targets": {"map/ui byte write"},
    },
    0x46410D: {"last_arg2": 0x190},
    0x4651CB: {"last_arg2": 0xD7},
    0x46522D: {"last_arg2": 0xD8},
}

IGNORED_WRITES = {"ds:0x48c5ac"}

NON_BUILDER_CALLS = {
    0x4034DC, 0x4034FC, 0x40923C, 0x413098, 0x4130B8, 0x4130D8,
    0x4130F8,
    0x413500, 0x4135D4, 0x419E4C,
    0x44C04C,  # idou player animation
    0x450758,  # VCL property setters / text scaling, item deletion
    0x44BC0C,  # timer2 start; no command-buffer append
}

DEFAULT_EXE = Path.home() / (
    ".wine/drive_c/Program Files (x86)/Tower of the Sorcerer/TSW.exe")


def parse_hex(text: str) -> int:
    return int(str(text), 16)


def target_address(op: dict) -> int | None:
    m = re.match(r"^(0x[0-9a-f]+)", str(op.get("target", "")))
    return int(m.group(1), 16) if m else None


def expression_value(expr: str, aux: dict[str, int]) -> int | None:
    if not expr:
        return None
    m = re.match(r"^(?:(?:WORD|DWORD|BYTE) PTR )?ds:0x([0-9a-f]+)$",
                 expr.strip(), re.IGNORECASE)
    if not m:
        return None
    name = event_snapshot.AUX_ADDRS.get(int(m.group(1), 16))
    return int(aux.get(name, 0)) if name else None


class Composer:
    def __init__(self, callees: dict[int, dict], exe: Path, map_block: bytes):
        self.callees = callees
        self.map_block = bytearray(map_block)
        self.commands: list[dict] = []
        self.unresolved: list[dict] = []
        self.calls: list[dict] = []
        self.visited: list[int] = []
        self.globals: dict[int, int] = {}
        self.data, self.image_base, self.sections = native.parse_pe(exe)

    def read_vma(self, address: int, size: int) -> bytes:
        for section in self.sections:
            start = self.image_base + section["v_addr"]
            length = max(section["v_size"], section["raw_size"])
            if start <= address < start + length:
                offset = section["raw_ptr"] + (address - start)
                return self.data[offset:offset + size]
        raise ValueError(f"address 0x{address:08x} is not file-backed")

    def special_successors(self, block: int, state: list[int],
                           aux: dict[str, int]) -> list[int] | None:
        switch = SWITCH_CONTRACTS.get(block)
        if switch:
            floor = state[switch["index_state"]]
            if 0 <= floor < 0x100:
                index = self.read_vma(switch["byte_table"] + floor, 1)[0]
                target = int.from_bytes(
                    self.read_vma(switch["pointer_table"] + index * 4, 4),
                    "little")
                return [target]
            return [switch["default"]]
        contract = PATH_CONTRACTS.get(block)
        if not contract:
            return None
        kind = contract["kind"]
        if kind == "floor_max":
            floor = state[0x010 // 4]
            return [contract["true"] if floor > contract["max"]
                    else contract["false"]]
        if kind == "state_selector":
            selector = int(aux.get(contract["selector"], 0))
            value = state[selector] if 0 <= selector < len(state) else 0
            if value == 0:
                return [contract["zero"]]
            if value == 1:
                return [contract["one"]]
            if value == 2:
                return [contract["two"]]
            return [contract["other"]]
        if kind == "state_value":
            value = state[contract["index"]]
            if value == 0:
                return [contract["zero"]]
            if value == 1:
                return [contract["one"]]
            return [contract["other"]]
        if kind == "floor_equal":
            floor = state[0x010 // 4]
            return [contract["true"] if floor == contract["value"]
                    else contract["false"]]
        if kind == "aux_equal":
            value = int(aux.get(contract["name"], 0))
            return [contract["true"] if value == contract["value"]
                    else contract["false"]]
        if kind == "byte_global":
            value = self.read_vma(contract["address"], 1)[0]
            if value == 0:
                return [contract["zero"]]
            if value == 1:
                return [contract["one"]]
            return [contract["other"]]
        if kind == "aux_ge":
            value = int(aux.get(contract["name"], 0))
            return [contract["true"] if value >= contract["value"]
                    else contract["false"]]
        if kind == "aux_not_equal":
            value = int(aux.get(contract["name"], 0))
            return [contract["true"] if value != contract["value"]
                    else contract["false"]]
        if kind == "dword_global_ge":
            value = self.globals.get(contract["address"], 0)
            return [contract["true"] if value >= contract["value"]
                    else contract["false"]]
        if kind == "form_width":
            value = int(contract["width"]) - int(contract["first"])
            if value == 0:
                return [contract["zero"]]
            if value - int(contract["second"]) == 0:
                return [contract["one"]]
            return [contract["other"]]
        if kind == "local_ge":
            return [contract["true"] if int(contract["value"]) >= int(
                contract["threshold"]) else contract["false"]]
        if kind == "facing_selector":
            value = state[int(contract["index"])]
            return [contract.get({1: "one", 2: "two", 3: "three",
                                  4: "four"}.get(value, "other"))]
        if kind == "tile_switch":
            floor = state[int(contract["floor_state"])]
            target_index = int(aux.get(contract["target_aux"], 0))
            offset = 2 + floor * 123 + target_index
            tile = self.map_block[offset] if 0 <= offset < len(self.map_block) else 0
            if tile >= int(contract["high"]):
                return [contract["high_target"]]
            if tile > int(contract["max"]):
                return [contract["default"]]
            target = int.from_bytes(
                self.read_vma(int(contract["pointer_table"]) + tile * 4, 4),
                "little")
            return [target]
        return None

    def append(self, entry: dict, overrides: dict[str, int | None],
               aux: dict[str, int]) -> None:
        opcode = entry.get("opcode")
        arg1 = entry.get("arg1")
        arg2 = entry.get("arg2")
        if arg1 is None:
            arg1 = overrides.get("arg1")
            if arg1 is None:
                arg1 = expression_value(str(entry.get("arg1_expr", "")), aux)
        if arg2 is None:
            arg2 = overrides.get("arg2")
            if arg2 is None:
                arg2 = expression_value(str(entry.get("arg2_expr", "")), aux)
        if opcode is None:
            self.unresolved.append({"reason": "missing_opcode", **entry})
            return
        self.commands.append({
            "opcode": int(opcode),
            "arg1": None if arg1 is None else int(arg1),
            "arg2": None if arg2 is None else int(arg2),
            "source": entry.get("instruction", ""),
        })

    def process_loop(self, events: list[tuple[int, dict, str]],
                     contract: dict, overrides: dict[str, int | None],
                     aux: dict[str, int]) -> None:
        counter = int(contract["counter_initial"])
        final = int(contract["counter_final"])
        step = int(contract["step"])
        offsets = list(contract["arg2_offsets"])
        while (counter >= final if step < 0 else counter <= final):
            offset_index = 0
            for _, event, kind in events:
                if kind != "append":
                    continue
                entry = dict(event)
                if (entry.get("opcode") == 0x0B and entry.get("arg2") is None
                        and offset_index < len(offsets)):
                    offset = offsets[offset_index]
                    offset_index += 1
                    entry["arg2"] = (counter * 6 + int(contract["arg2_base"])
                                     + offset)
                self.append(entry, overrides, aux)
            counter += step

    def inline(self, bundle: dict, entry_block: int, overrides: dict[str, int | None],
               depth: int, state: list[int], aux: dict[str, int],
               resolved_writes: set[str]) -> None:
        if depth > 8:
            self.unresolved.append({"reason": "call_depth"})
            return
        self.walk(bundle, entry_block, state, aux, overrides, depth + 1,
                  resolved_writes)

    def walk(self, ir: dict, entry_block: int, state: list[int],
             aux: dict[str, int], overrides: dict[str, int | None],
             depth: int = 0, resolved_writes: set[str] | None = None) -> None:
        resolved_writes = resolved_writes or set()
        blocks = {parse_hex(b["block_start"]): b for b in ir.get("blocks", [])}
        if entry_block not in blocks:
            self.unresolved.append({
                "reason": "invalid_entry", "block": f"0x{entry_block:08x}",
            })
            return
        events_by_block: dict[int, list[tuple[int, dict, str]]] = {}
        for entry in ir.get("appends", []):
            block = parse_hex(entry.get("block_start", "0x0"))
            addr = entry.get("opcode_addr")
            if not addr:
                continue
            events_by_block.setdefault(block, []).append(
                (parse_hex(addr), entry, "append"))
        for op in ir.get("ops", []):
            block = parse_hex(op["block_start"])
            events_by_block.setdefault(block, []).append(
                (parse_hex(op["address"]), op, "op"))
        for items in events_by_block.values():
            items.sort(key=lambda x: x[0])

        queue = [entry_block]
        seen: set[int] = set()
        while queue:
            block = queue.pop()
            if block in seen:
                self.unresolved.append({
                    "reason": "loop_back_edge", "block": f"0x{block:08x}",
                })
                continue
            seen.add(block)
            self.visited.append(block)
            effect = BLOCK_EFFECTS.get(block)
            block_resolved = set(resolved_writes)
            if effect:
                if "percent" in effect:
                    percent = int(effect["percent"])
                    for index in effect["stat_indices"]:
                        state[index] += int(state[index] * percent / 100)
                if "last_arg1" in effect and self.commands:
                    self.commands[-1]["arg1"] = int(effect["last_arg1"])
                if "last_arg2" in effect and self.commands:
                    self.commands[-1]["arg2"] = int(effect["last_arg2"])
                if "map_tile" in effect:
                    tile_effect = effect["map_tile"]
                    floor = state[int(tile_effect["floor_state"])]
                    target = int(aux.get(tile_effect["target_aux"], 0))
                    offset = 2 + floor * 123 + target
                    if 0 <= offset < len(self.map_block):
                        self.map_block[offset] = int(tile_effect["value"])
                block_resolved |= set(effect.get("resolved_targets", set()))
            loop = LOOP_CONTRACTS.get(block)
            if loop:
                self.process_loop(events_by_block.get(block, []), loop,
                                  overrides, aux)
                for part in str(blocks[block].get("successors", "")).split(" | "):
                    m = re.match(r"^0x([0-9a-f]+):(.*)$", part.strip())
                    if not m:
                        continue
                    target = int(m.group(1), 16)
                    if target != block and target in blocks:
                        queue.append(target)
                continue
            stop = False
            for _, event, kind in events_by_block.get(block, []):
                if kind == "append":
                    self.append(event, overrides, aux)
                    continue
                op_kind = event.get("kind")
                if op_kind == "write_global":
                    applied = synthetic.apply_write(event, state, aux)
                    if applied is None:
                        target = str(event.get("target", ""))
                        if target in block_resolved or target in IGNORED_WRITES:
                            continue
                        self.unresolved.append({
                            **event, "reason": "unsupported_write",
                        })
                    continue
                if op_kind != "call":
                    if (op_kind == "write_mem"
                            and str(event.get("target", "")) not in
                            block_resolved):
                        self.unresolved.append({
                            **event, "reason": "unresolved_map_write",
                        })
                    continue
                self.calls.append(event)
                target = target_address(event)
                if target is None:
                    self.unresolved.append({**event, "reason": "bad_call"})
                    continue
                contract = CALL_CONTRACTS.get(target)
                if contract and target in self.callees:
                    consumed = None
                    if contract.get("consumes_last"):
                        if not self.commands:
                            self.unresolved.append({
                                **event, "reason": "missing_consumed_command",
                            })
                            stop = True
                            break
                        consumed = self.commands.pop()
                    child_over = dict(overrides)
                    if consumed is not None:
                        for key, index in (("arg1", 1), ("arg2", 2)):
                            source = contract.get(f"last_{key}_to")
                            if source == key:
                                child_over[key] = consumed[key]
                        for aux_name, command_index in contract.get(
                                "aux_from_last", {}).items():
                            aux[aux_name] = consumed[
                                "arg1" if command_index == 1 else "arg2"]
                    bundle = self.callees[target]
                    starts = [parse_hex(b["block_start"])
                              for b in bundle.get("blocks", [])]
                    child_resolved = set(contract.get("resolved_writes", set()))
                    self.inline(bundle, min(starts), child_over, depth,
                                state, aux, child_resolved)
                    continue
                if target in self.callees:
                    self.unresolved.append({
                        **event, "reason": "callee_without_contract",
                    })
                    stop = True
                    break
                if (target not in synthetic.PRESENTATION_CALL_ADDRS
                        and target not in NON_BUILDER_CALLS):
                    self.unresolved.append({
                        **event, "reason": "unclassified_call",
                    })
            if stop:
                continue
            special = self.special_successors(block, state, aux)
            if special is not None:
                for target in reversed(special):
                    if target in seen:
                        self.unresolved.append({
                            "reason": "loop_back_edge",
                            "block": f"0x{block:08x}",
                            "target": f"0x{target:08x}",
                        })
                    else:
                        queue.append(target)
                continue
            taken: list[int] = []
            for part in str(blocks[block].get("successors", "")).split(" | "):
                part = part.strip()
                m = re.match(r"^0x([0-9a-f]+):(.*)$", part)
                if not m:
                    continue
                target = int(m.group(1), 16)
                if target not in blocks:
                    continue
                truth = synthetic.edge_truth(m.group(2), state, aux)
                if truth is False:
                    continue
                if truth is None:
                    self.unresolved.append({
                        "reason": "unknown_edge",
                        "block": f"0x{block:08x}",
                        "target": f"0x{target:08x}",
                        "label": m.group(2),
                    })
                    continue
                if target in seen:
                    self.unresolved.append({
                        "reason": "loop_back_edge",
                        "block": f"0x{block:08x}",
                        "target": f"0x{target:08x}",
                    })
                    continue
                taken.append(target)
            for target in reversed(taken):
                queue.append(target)


def load_callees(directory: Path) -> dict[int, dict]:
    out = {}
    for path in sorted(directory.glob("*/builder_ir.json")):
        bundle = json.loads(path.read_text(encoding="utf-8"))
        method = str(bundle.get("method", ""))
        m = re.search(r"@([0-9a-fA-F]+)$", method)
        if m:
            out[int(m.group(1), 16)] = bundle
    return out


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ir", type=Path)
    ap.add_argument("--callee-dir", type=Path, required=True)
    ap.add_argument("--snapshot", type=Path, required=True)
    ap.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    ap.add_argument("--entry-block", type=lambda x: int(x, 16), default=None)
    ap.add_argument("-o", "--output", type=Path)
    args = ap.parse_args()

    ir = json.loads(args.ir.read_text(encoding="utf-8"))
    snapshot = event_snapshot.Snapshot.from_manifest(
        json.loads(args.snapshot.read_text(encoding="utf-8")))
    state = list(snapshot.state)
    aux = snapshot.aux_with_derived()
    entry = args.entry_block
    if entry is None:
        entry = min(parse_hex(b["block_start"]) for b in ir["blocks"])
    composer = Composer(load_callees(args.callee_dir), args.exe,
                        snapshot.map_block)
    composer.walk(ir, entry, state, aux, {})
    for index, command in enumerate(composer.commands):
        if command["arg1"] is None or command["arg2"] is None:
            composer.unresolved.append({
                "reason": "unresolved_argument",
                "command_index": index,
                **command,
            })
    words = [[c["opcode"], c["arg1"], c["arg2"]] for c in composer.commands]
    result = {
        "schema_version": 1,
        "method": ir.get("method"),
        "entry_block": f"0x{entry:08x}",
        "commands": len(words),
        "command_words": words,
        "unresolved": composer.unresolved,
        "calls": composer.calls,
        "visited_blocks": [f"0x{b:08x}" for b in composer.visited],
        "aux_final": aux,
    }
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + "\n",
                               encoding="utf-8")
    print(f"commands: {len(words)} unresolved: {len(composer.unresolved)}")
    if args.output:
        print(f"wrote {args.output}")
    return 0 if not composer.unresolved else 1


if __name__ == "__main__":
    raise SystemExit(main())
