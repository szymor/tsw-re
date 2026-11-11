#!/usr/bin/env python3
"""First-pass CFG-aware append-site lifter for one event handler.

This does not yet produce final executable timelines.  It groups the
command-buffer stores discovered by `scan_event_buffers.py` into append rows,
resolves immediate opcode/argument values where possible, and labels each row
with its containing basic block.  With ``--dir`` it also writes block-edge
witnesses and builder-side call/guard/write rows.

Usage:
    python3 docs/re/tools/lift_event_handler.py "$GAME/TSW.exe" --method floor10
    python3 docs/re/tools/lift_event_handler.py "$GAME/TSW.exe" --method roujin -o /tmp/roujin.tsv
    python3 docs/re/tools/lift_event_handler.py "$GAME/TSW.exe" --method floor10 --dir /tmp/floor10-ir
"""

from __future__ import annotations

import argparse
import collections
import os
import re
import sys
from pathlib import Path

import scan_event_buffers as buffers
import scan_event_triggers as triggers

EXPECTED_SHA256 = buffers.EXPECTED_SHA256


def parse_value(text: str) -> int | None:
    if "," not in text:
        return None
    token = text.rsplit(",", 1)[1].strip()
    if re.match(r"^0x[0-9a-f]+$", token, re.IGNORECASE):
        return int(token, 16)
    if re.match(r"^[0-9]+$", token):
        return int(token, 10)
    return None


def register_source(text: str) -> str | None:
    if "," not in text:
        return None
    token = text.rsplit(",", 1)[1].strip()
    return token if token in buffers.CANON else None


def parse_immediate_src(src: str | None) -> int | None:
    if not src:
        return None
    if re.match(r"^0x[0-9a-f]+$", src, re.IGNORECASE):
        return int(src, 16)
    if re.match(r"^[0-9]+$", src):
        return int(src, 10)
    return None


def resolve_register(insns: list[tuple[int, str]], idx: int, reg: str,
                     min_index: int = 0) -> int | None:
    target = buffers.CANON.get(reg, reg)
    target_bits = 8 if reg in buffers.REG8 else 16 if reg in buffers.REG16 else 32
    j = idx - 1
    seen = 0
    while j >= min_index and seen < 12:
        text = insns[j][1]
        op = text.split(" ", 1)[0]
        if op in ("call", "ret", "retn", "retf") or op in buffers.COND_BRANCHES or op == "jmp":
            return None
        dest, src = buffers.dest_src(text)
        if dest in buffers.CANON and buffers.CANON[dest] == target:
            if op == "mov":
                if target_bits == 32 and dest in buffers.REG32:
                    return parse_immediate_src(src)
                if target_bits == 16 and dest in buffers.REG16:
                    value = parse_immediate_src(src)
                    return None if value is None else value & 0xFFFF
                # 8-bit or partial write invalidates the wider register.
                return None
            if op == "xor" and src and buffers.CANON.get(src) == target:
                if dest in buffers.REG32:
                    return 0
                if target_bits <= 16 and dest in buffers.REG16 and dest == reg:
                    return 0
            return None
        if op in ("inc", "dec", "add", "sub", "and", "or", "movzx", "movsx"):
            # A non-matching write may still touch a subregister alias.
            pass
        j -= 1
        seen += 1
    return None


def block_start_for(blocks: list[tuple[int, int]], addr: int) -> int:
    for start, end in blocks:
        if start <= addr <= end:
            return start
    return -1


def lift(exe: Path, method_prefix: str):
    true_methods = buffers.load_methods(
        Path(__file__).resolve().parents[1] / "method-table.tsv")
    methods = triggers.method_labels(true_methods)
    entries = ({addr for addr, _ in methods}
               | {addr for addr, _ in buffers.EVENT_HANDLERS})
    insns = buffers.disassemble(exe, entries)
    table_edges, table_ranges = buffers.recover_indirect_edges(exe, insns)
    entry_addrs = entries | {t for targets in table_edges.values() for t in targets}
    insns = buffers.disassemble(exe, entry_addrs)
    table_edges, table_ranges = buffers.recover_indirect_edges(exe, insns)
    accesses, _base, _unres = buffers.scan(
        insns, methods, true_methods,
        {addr for addr, _ in buffers.EVENT_HANDLERS},
        table_edges, table_ranges + buffers.KNOWN_DATA_RANGES,
    )
    blocks, _, _ = buffers.build_blocks(insns, entry_addrs)
    addr_to_index = {addr: i for i, (addr, _) in enumerate(insns)}

    handler = [a for a in accesses if str(a["method"]).startswith(method_prefix + "@")]
    handler.sort(key=lambda a: int(a["address"]))
    writes = [a for a in handler
              if a["target"] in ("cmd_opcode", "cmd_arg1", "cmd_arg2")
              and a["access"] in ("write", "read_write")]
    rows: list[dict[str, object]] = []
    seq = 0
    for i, op in enumerate(writes):
        if op["target"] != "cmd_opcode":
            continue
        op_addr = int(op["address"])
        op_idx = addr_to_index.get(op_addr, 0)
        op_block = block_start_for(blocks, op_idx)
        if op_block < 0:
            continue
        block_end = next((end for start, end in blocks if start == op_block), op_idx)
        next_op_addr = insns[block_end][0] + 1
        for w in writes[i + 1:]:
            w_idx = addr_to_index.get(int(w["address"]), 0)
            if block_start_for(blocks, w_idx) != op_block:
                continue
            if w["target"] == "cmd_opcode":
                next_op_addr = int(w["address"])
                break
        args: dict[str, int | None] = {"cmd_arg1": None, "cmd_arg2": None}
        arg_sources: dict[str, int] = {}
        for w in writes[i + 1:]:
            a = int(w["address"])
            w_idx = addr_to_index.get(a, 0)
            if a >= next_op_addr or block_start_for(blocks, w_idx) != op_block:
                continue
            if w["target"] in args and w["target"] not in arg_sources:
                value = parse_value(str(w["instruction"]))
                if value is None:
                    reg = register_source(str(w["instruction"]))
                    if reg:
                        value = resolve_register(insns, w_idx, reg, op_block)
                args[w["target"]] = value
                arg_sources[w["target"]] = a
        rows.append({
            "method": op["method"],
            "block_start": insns[op_block][0],
            "seq": seq,
            "opcode": parse_value(str(op["instruction"])),
            "arg1": args["cmd_arg1"],
            "arg2": args["cmd_arg2"],
            "opcode_addr": op_addr,
            "arg1_addr": arg_sources.get("cmd_arg1", 0),
            "arg2_addr": arg_sources.get("cmd_arg2", 0),
            "instruction": op["instruction"],
        })
        seq += 1
    return rows, insns, blocks


def method_range(methods: list[tuple[int, str]], prefix: str):
    entries = sorted(set(methods) | set(buffers.EVENT_HANDLERS))
    for i, (addr, name) in enumerate(entries):
        if name == prefix:
            end = entries[i + 1][0] if i + 1 < len(entries) else 0x488E94
            return addr, end
    return None, None


def block_rows(insns, blocks, blocks_range):
    start, end = blocks_range
    addr_to_block = {insns[s][0]: bi for bi, (s, _) in enumerate(blocks)}
    succ: dict[int, list[tuple[int, str]]] = {i: [] for i in range(len(blocks))}
    for bi, (bs, be) in enumerate(blocks):
        if not (start <= insns[bs][0] < end):
            continue
        last = insns[be][1]
        op = last.split(" ", 1)[0]
        nxt = bi + 1 if bi + 1 < len(blocks) else None
        if op in buffers.COND_BRANCHES:
            target = buffers.direct_target(last)
            if target in addr_to_block:
                succ[bi].append((addr_to_block[target], f"branch:{last}"))
            if nxt is not None:
                succ[bi].append((nxt, f"fallthrough:not({last})"))
        elif op == "jmp":
            target = buffers.direct_target(last)
            if target in addr_to_block:
                succ[bi].append((addr_to_block[target], f"jump:{last}"))
            else:
                succ[bi].append((-1, "switch:indirect"))
        elif op in ("ret", "retn", "retf", "iretd"):
            pass
        elif nxt is not None:
            succ[bi].append((nxt, "fallthrough"))
    preds: dict[int, list[tuple[int, str]]] = {i: [] for i in range(len(blocks))}
    for src, edges in succ.items():
        for dst, label in edges:
            if dst >= 0:
                preds[dst].append((src, label))
    rows = []
    for bi, (bs, be) in enumerate(blocks):
        block_start = insns[bs][0]
        if not (start <= block_start < end):
            continue
        pred_text = " | ".join(
            f"0x{insns[blocks[p][0]][0]:08x}:{label}" for p, label in preds[bi][:4]
        ) or "entry-or-unreached"
        succ_text = " | ".join(
            "unresolved-switch" if d < 0 else
            f"0x{insns[blocks[d][0]][0]:08x}:{label}"
            for d, label in succ[bi][:4]
        )
        rows.append({
            "block_start": block_start,
            "block_end": insns[be][0],
            "last_instruction": insns[be][1],
            "predecessors": pred_text,
            "successors": succ_text,
        })
    return rows


def ops_rows(insns, blocks, blocks_range, methods):
    start, end = blocks_range
    rows = []
    for bs, be in blocks:
        if not (start <= insns[bs][0] < end):
            continue
        for addr, text in insns[bs:be + 1]:
            kind = None
            target = ""
            m = re.match(r"^call\s+0x([0-9a-f]+)$", text)
            if m:
                target_addr = int(m.group(1), 16)
                kind = "call"
                target = f"0x{target_addr:08x} {buffers.nearest_method(methods, target_addr)}"
            else:
                op = text.split(" ", 1)[0]
                if op in buffers.COND_BRANCHES:
                    kind = "guard"
                    target = text
                elif (re.search(r"ds:0x4b[89][0-9a-f]{3}", text, re.IGNORECASE)
                      and op not in ("cmp", "test")):
                    dest, _src = buffers.dest_src(text)
                    if buffers.is_memory(dest):
                        kind = "write_global"
                        target = re.search(r"ds:0x4b[89][0-9a-f]{3}", text,
                                           re.IGNORECASE).group(0)  # type: ignore[union-attr]
                elif re.match(r"^mov\s+BYTE PTR \[", text):
                    kind = "write_mem"
                    target = "map/ui byte write"
            if kind:
                rows.append({
                    "block_start": insns[bs][0],
                    "address": addr,
                    "kind": kind,
                    "target": target,
                    "instruction": text,
                })
    return rows


def write_blocks(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        f.write("block_start\tblock_end\tlast_instruction\tpredecessors\tsuccessors\n")
        for r in rows:
            f.write(f"0x{int(r['block_start']):08x}\t0x{int(r['block_end']):08x}\t"
                    f"{r['last_instruction']}\t{r['predecessors']}\t{r['successors']}\n")


def write_ops(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        f.write("block_start\taddress\tkind\ttarget\tinstruction\n")
        for r in rows:
            f.write(f"0x{int(r['block_start']):08x}\t0x{int(r['address']):08x}\t"
                    f"{r['kind']}\t{r['target']}\t{r['instruction']}\n")


def write_rows(path: Path, rows: list[dict[str, object]],
               block_entries: dict[int, str] | None = None) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        header = ("method\tblock_start\tseq\topcode\targ1\targ2\topcode_addr\t"
                  "arg1_addr\targ2_addr\tinstruction")
        if block_entries is not None:
            header += "\tblock_entry"
        f.write(header + "\n")
        for r in rows:
            def val(v):
                return "?" if v is None else f"0x{int(v):x}"
            line = (f"{r['method']}\t0x{int(r['block_start']):08x}\t{r['seq']}\t"
                    f"{val(r['opcode'])}\t{val(r['arg1'])}\t{val(r['arg2'])}\t"
                    f"0x{int(r['opcode_addr']):08x}\t0x{int(r['arg1_addr']):08x}\t"
                    f"0x{int(r['arg2_addr']):08x}\t{r['instruction']}")
            if block_entries is not None:
                line += "\t" + block_entries.get(int(r["block_start"]), "")
            f.write(line + "\n")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("exe", type=Path)
    ap.add_argument("--method", required=True,
                    help="method prefix, e.g. floor10, floor40, roujin, openning2")
    ap.add_argument("-o", "--output", type=Path)
    ap.add_argument("--dir", type=Path,
                    help="write appends.tsv, blocks.tsv and ops.tsv here")
    ap.add_argument("--summary", action="store_true")
    ap.add_argument("--allow-other", action="store_true")
    args = ap.parse_args()

    if not args.exe.exists():
        print(f"error: {args.exe} does not exist", file=sys.stderr)
        return 2
    digest = buffers.sha256(args.exe)
    if digest != EXPECTED_SHA256:
        if not args.allow_other:
            print(f"error: binary hash {digest} does not match {EXPECTED_SHA256}",
                  file=sys.stderr)
            return 3
        print("warning: scanning an unpinned binary", file=sys.stderr)

    rows, insns, blocks = lift(args.exe, args.method)
    if args.summary:
        print(f"method: {args.method}")
        print(f"append rows: {len(rows)}")
        print(f"blocks: {len({r['block_start'] for r in rows})}")
        hist = collections.Counter(r["opcode"] for r in rows)
        print("opcodes: " + ", ".join(
            f"{'?' if k is None else hex(k)}={v}" for k, v in hist.most_common()))
        print(f"resolved arg1: {sum(1 for r in rows if r['arg1'] is not None)}")
        print(f"resolved arg2: {sum(1 for r in rows if r['arg2'] is not None)}")
        return 0

    true_methods = buffers.load_methods(
        Path(__file__).resolve().parents[1] / "method-table.tsv")
    methods = triggers.method_labels(true_methods)
    method_start, method_end = method_range(methods, args.method)
    if method_start is None:
        print(f"error: method {args.method} not found", file=sys.stderr)
        return 4
    b_rows = block_rows(insns, blocks, (method_start, method_end))
    o_rows = ops_rows(insns, blocks, (method_start, method_end), methods)
    entry_by_start = {int(r["block_start"]): str(r["predecessors"]) for r in b_rows}

    if args.dir:
        outdir = args.dir
        outdir.mkdir(parents=True, exist_ok=True)
        write_rows(outdir / "appends.tsv", rows, entry_by_start)
        write_blocks(outdir / "blocks.tsv", b_rows)
        write_ops(outdir / "ops.tsv", o_rows)
        print(f"wrote {outdir}/appends.tsv, blocks.tsv, ops.tsv")
        return 0

    cache = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "tsw"
    output = args.output or cache / f"lift_{args.method}.tsv"
    write_rows(output, rows, entry_by_start)
    print(f"wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
