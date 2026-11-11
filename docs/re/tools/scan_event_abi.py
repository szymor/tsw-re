#!/usr/bin/env python3
"""Inventory event entry-point call-site register setup.

Delphi's register convention passes the first arguments in eax/edx/ecx (and
Self in eax for published methods).  This tool lists every direct call/jmp to
the tracked event entries with the immediately preceding instructions and the
last visible setup of eax/edx/ecx.  It is evidence for the oracle entry
contract, not a complete ABI proof.

Usage:
    python3 docs/re/tools/scan_event_abi.py /path/to/TSW.exe --summary
    python3 docs/re/tools/scan_event_abi.py /path/to/TSW.exe -o /tmp/opencode
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
CONTEXT_COUNT = 6


def previous_writes(insns: list[tuple[int, str]], index: int) -> dict[str, str | None]:
    out: dict[str, str | None] = {"eax": None, "edx": None, "ecx": None}
    seen = set()
    non_writes = {
        "cmp", "test", "push", "jmp", "je", "jne", "ja", "jae",
        "jb", "jbe", "jg", "jge", "jl", "jle", "jz", "jnz", "call",
    }
    j = index - 1
    while j >= 0 and len(seen) < 3:
        text = insns[j][1]
        op = text.split(" ", 1)[0]
        if op in non_writes:
            if op == "call":
                break
            j -= 1
            continue
        dest, _src = buffers.dest_src(text)
        if dest in buffers.CANON:
            reg = buffers.CANON[dest]
            if reg in out and reg not in seen:
                out[reg] = text
                seen.add(reg)
        j -= 1
    return out


def scan(exe: Path) -> list[dict[str, object]]:
    true_methods = buffers.load_methods(
        Path(__file__).resolve().parents[1] / "method-table.tsv")
    methods = triggers.method_labels(true_methods)
    entries = {addr: name for addr, name in true_methods if name in triggers.ENTRY_POINTS}
    tables, table_ranges = triggers.decode_tables(exe)
    table_targets = {int(r["target"]) for r in tables}
    entry_addrs = ({addr for addr, _ in methods} | set(entries)
                   | table_targets)
    insns = buffers.disassemble(exe, entry_addrs)
    rows: list[dict[str, object]] = []
    for i, (addr, text) in enumerate(insns):
        if triggers.in_ranges(addr, table_ranges + buffers.KNOWN_DATA_RANGES):
            continue
        m = re.match(r"^(call|jmp)\s+0x([0-9a-f]+)$", text)
        if not m:
            continue
        target = int(m.group(2), 16)
        if target not in entries:
            continue
        writes = previous_writes(insns, i)
        context = [insns[j][1] for j in range(max(0, i - CONTEXT_COUNT), i)]
        rows.append({
            "caller": buffers.nearest_method(methods, addr),
            "call_addr": addr,
            "kind": m.group(1),
            "callee": entries[target],
            "callee_addr": target,
            "eax_setup": writes["eax"] or "",
            "edx_setup": writes["edx"] or "",
            "ecx_setup": writes["ecx"] or "",
            "context": " ; ".join(context),
        })
    return rows


def write_rows(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        f.write("caller\tcall_addr\tkind\tcallee\tcallee_addr\teax_setup\t"
                "edx_setup\tecx_setup\tcontext\n")
        for r in rows:
            f.write("\t".join(str(r[k]) for k in (
                "caller", "call_addr", "kind", "callee", "callee_addr",
                "eax_setup", "edx_setup", "ecx_setup", "context")) + "\n")


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
    digest = buffers.sha256(args.exe)
    if digest != EXPECTED_SHA256:
        if not args.allow_other:
            print(f"error: binary hash {digest} does not match {EXPECTED_SHA256}",
                  file=sys.stderr)
            return 3
        print("warning: scanning an unpinned binary", file=sys.stderr)

    rows = scan(args.exe)
    if args.summary:
        print(f"sha256: {digest}")
        print(f"call sites: {len(rows)}")
        by_callee = collections.Counter(str(r["callee"]) for r in rows)
        for callee, count in by_callee.most_common():
            setups = collections.Counter(
                (str(r["eax_setup"]), str(r["edx_setup"]), str(r["ecx_setup"]))
                for r in rows if r["callee"] == callee
            )
            print(f"  {callee}: {count}")
            for setup, n in setups.most_common(3):
                print(f"    eax={setup[0] or '-'} edx={setup[1] or '-'} "
                      f"ecx={setup[2] or '-'} ({n})")
        return 0

    cache = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "tsw"
    output = args.output or cache / "event_abi_call_sites.tsv"
    write_rows(output, rows)
    print(f"wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
