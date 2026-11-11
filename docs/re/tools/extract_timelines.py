#!/usr/bin/env python3
"""Extract the per-floor event timelines from the user's TSW.exe.

The event handlers are jump-table targets inside one large function.  Each
handler appends 6-byte commands to the in-memory timeline:

    opcode (u16), arg1 (u16), arg2 (u16)

This script disassembles the handler region with objdump and reconstructs the
command sequences.  Output is local analysis data; keep it out of git.

Usage:
    extract_timelines.py /path/to/TSW.exe -o timelines.txt
    extract_timelines.py /path/to/TSW.exe --summary
"""

import argparse
import collections
import re
import subprocess
import sys

CASES = [
    ("floor10", 0x46CBC7),
    ("floor15", 0x46F82E),
    ("floor20", 0x46F8F4),
    ("floor24", 0x4704EB),
    ("floor25", 0x4727B2),
    ("floor30", 0x4728EF),
    ("floor32", 0x472A2C),
    ("floor33", 0x4735A4),
    ("floor38", 0x47382C),
    ("floor40", 0x473961),
    ("floor42", 0x475FBA),
    ("floor43", 0x47687E),
    ("floor48", 0x476CDD),
    ("floor49", 0x476DC0),
    ("end", 0x4770A1),
]


def disassemble(exe):
    out = subprocess.check_output(
        ["objdump", "-d", "-M", "intel",
         "--start-address=0x46cbc7", "--stop-address=0x4770a1", exe],
        text=True, errors="replace",
    )
    insns = []
    for line in out.splitlines():
        m = re.match(r"\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)", line)
        if not m:
            continue
        insns.append((int(m.group(1), 16), re.sub(r"\s+", " ", m.group(3).strip())))
    return insns


def parse_value(src, regs):
    if src.startswith("0x") or re.match(r"^[0-9]+$", src):
        return int(src, 0)
    if src in regs:
        return regs[src]
    if src.startswith("WORD PTR ds:0x"):
        return "[" + src.split("ds:")[1].split("]")[0] + "]"
    return "?" + src


def extract(insns):
    cases = sorted(CASES, key=lambda x: x[1])
    result = {}
    for ci in range(len(cases) - 1):
        name, start = cases[ci]
        end = cases[ci + 1][1]
        window = [(a, t) for a, t in insns if start <= a < end]
        cmds = []
        regs = {}
        i = 0
        while i < len(window):
            addr, text = window[i]
            m = re.match(
                r"mov (e?[abcd]x|e?[sd]i|e?[sb]p|dx|cx|ax|bx),(0x[0-9a-f]+|[0-9]+)",
                text,
            )
            if m:
                regs[m.group(1)] = int(m.group(2), 0)
            m = re.match(r"xor (e?[abcd]x|dx|cx|ax|bx),\1$", text)
            if m:
                regs[m.group(1)] = 0
            if text.startswith("inc DWORD PTR [ebx]"):
                stores = []
                j = i + 1
                while j < len(window) and len(stores) < 3 and j < i + 16:
                    _a2, t2 = window[j]
                    if t2.startswith("mov WORD PTR [esi+eax*2"):
                        mm = re.search(r"\[esi\+eax\*2(?:\+0x([0-9a-f]+))?\]", t2)
                        off = int(mm.group(1), 16) if mm and mm.group(1) else 0
                        src = t2.split(",", 1)[1].strip() if "," in t2 else ""
                        stores.append((off, parse_value(src, regs)))
                    j += 1
                if len(stores) >= 3:
                    d = dict(stores)
                    cmds.append((addr, d.get(0, "?"), d.get(2, "?"), d.get(4, "?")))
                i = j
                continue
            i += 1
        result[name] = cmds
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("-o", "--output", default="timelines.txt")
    ap.add_argument("--summary", action="store_true")
    args = ap.parse_args()

    data = extract(disassemble(args.exe))
    if args.summary:
        for name, cmds in data.items():
            hist = collections.Counter(c[1] for c in cmds)
            print(f"{name}: {len(cmds)} commands")
            print("  opcodes:", dict(sorted(hist.items(), key=lambda kv: kv[0] if isinstance(kv[0], int) else 999)))
            subs = [c for c in cmds if c[1] == 11]
            if subs:
                print("  0xb sub-commands:", [c[2] for c in subs])
        return

    with open(args.output, "w", encoding="utf-8") as f:
        f.write("# Local extraction from the user's TSW.exe. Do not redistribute.\n")
        f.write("# floor\taddress\topcode\targ1\targ2\n")
        for name, cmds in data.items():
            for addr, op, a1, a2 in cmds:
                f.write(f"{name}\t{addr:08x}\t{op}\t{a1}\t{a2}\n")
    print(f"wrote {args.output}")


if __name__ == "__main__":
    main()
