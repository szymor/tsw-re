#!/usr/bin/env python3
"""Inventory the static event-timeline append sites in the user's TSW.exe.

The event handlers are jump-table targets inside one large function.  Each
handler appends 6-byte commands to the in-memory timeline:

    opcode (u16), arg1 (u16), arg2 (u16)

This script disassembles the handler region with objdump and reconstructs the
**append sites** in each handler address window.  It is deliberately static:
it does not follow branches or shared control-flow tails.  Treat the output as
an inventory of possible commands, not as a final executable per-floor script.

Usage:
    extract_timelines.py /path/to/TSW.exe -o append_sites.txt
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

# canonical 32-bit register for each spelling we may see
CANON = {
    "eax": "eax", "ax": "eax", "al": "eax", "ah": "eax",
    "ebx": "ebx", "bx": "ebx", "bl": "ebx", "bh": "ebx",
    "ecx": "ecx", "cx": "ecx", "cl": "ecx", "ch": "ecx",
    "edx": "edx", "dx": "edx", "dl": "edx", "dh": "edx",
    "esi": "esi", "si": "esi", "edi": "edi", "di": "edi",
    "ebp": "ebp", "bp": "ebp", "esp": "esp", "sp": "esp",
}


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


def canon(reg):
    return CANON.get(reg, reg)


def set_reg(regs, reg, value):
    """Set a register value, handling 16/32-bit aliases."""
    c = canon(reg)
    if reg in ("eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp"):
        regs[c] = value
    elif reg in ("ax", "bx", "cx", "dx", "si", "di", "bp", "sp"):
        old = regs.get(c, 0)
        old = old if isinstance(old, int) else 0
        regs[c] = (old & ~0xFFFF) | (value & 0xFFFF if isinstance(value, int) else value)
    else:
        # 8-bit or unknown: invalidate the canonical register
        regs[c] = None


def get_reg(regs, reg):
    c = canon(reg)
    value = regs.get(c)
    if value is None:
        return None
    if isinstance(value, str):
        return value
    if reg in ("ax", "bx", "cx", "dx", "si", "di", "bp", "sp"):
        return value & 0xFFFF
    if reg in ("al", "bl", "cl", "dl"):
        return value & 0xFF
    if reg in ("ah", "bh", "ch", "dh"):
        return (value >> 8) & 0xFF
    return value


def apply_instruction(text, regs):
    """Update the simple register model before resolving a store."""
    m = re.match(r"mov ([a-z]+),(.+)", text)
    if m:
        dest, src = m.group(1), m.group(2).strip()
        if dest in CANON:
            if re.match(r"^0x[0-9a-f]+$", src) or re.match(r"^[0-9]+$", src):
                set_reg(regs, dest, int(src, 0))
            elif src.startswith("WORD PTR ds:0x"):
                set_reg(regs, dest, "[" + src.split("ds:")[1].split("]")[0] + "]")
            elif src in CANON:
                set_reg(regs, dest, get_reg(regs, src))
            else:
                set_reg(regs, dest, None)
        return
    m = re.match(r"xor ([a-z]+),\1$", text)
    if m and m.group(1) in CANON:
        set_reg(regs, m.group(1), 0)
        return
    m = re.match(r"(inc|dec|add|sub|and|or|shl|shr|sar|imul|lea) ([a-z]+)", text)
    if m and m.group(2) in CANON:
        set_reg(regs, m.group(2), None)
        return
    if text.startswith("call "):
        # volatile registers may be clobbered by a call
        for r in ("eax", "ecx", "edx"):
            regs[r] = None


def parse_value(src, regs):
    if re.match(r"^0x[0-9a-f]+$", src) or re.match(r"^[0-9]+$", src):
        return int(src, 0)
    if src in CANON:
        value = get_reg(regs, src)
        return "?" + src if value is None else value
    if src.startswith("WORD PTR ds:0x"):
        return "[" + src.split("ds:")[1].split("]")[0] + "]"
    return "?" + src


def extract(insns):
    cases = sorted(CASES, key=lambda x: x[1])
    result = {}
    diagnostics = []
    for ci in range(len(cases) - 1):
        name, start = cases[ci]
        end = cases[ci + 1][1]
        window = [(a, t) for a, t in insns if start <= a < end]
        cmds = []
        regs = {}
        i = 0
        while i < len(window):
            addr, text = window[i]
            if not text.startswith("inc DWORD PTR [ebx]"):
                apply_instruction(text, regs)
                i += 1
                continue

            stores = {}
            j = i + 1
            advance = len(window)
            while j < len(window):
                a2, t2 = window[j]
                if t2.startswith("inc DWORD PTR [ebx]"):
                    advance = j
                    break
                apply_instruction(t2, regs)
                if t2.startswith("mov WORD PTR [esi+eax*2"):
                    mm = re.search(r"\[esi\+eax\*2(?:\+0x([0-9a-f]+))?\]", t2)
                    off = int(mm.group(1), 16) if mm and mm.group(1) else 0
                    src = t2.split(",", 1)[1].strip() if "," in t2 else ""
                    stores[off] = parse_value(src, regs)
                if {0, 2, 4} <= set(stores):
                    advance = j + 1
                    break
                j += 1

            if {0, 2, 4} <= set(stores):
                cmds.append((addr, stores[0], stores[2], stores[4]))
            else:
                diagnostics.append((addr, stores))
            i = advance
        result[name] = cmds
    return result, diagnostics


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("-o", "--output", default="append_sites.txt")
    ap.add_argument("--summary", action="store_true")
    args = ap.parse_args()

    data, diagnostics = extract(disassemble(args.exe))
    if diagnostics:
        print(f"warning: {len(diagnostics)} incomplete append sites", file=sys.stderr)
        for addr, stores in diagnostics[:10]:
            print(f"  {addr:08x}: {stores}", file=sys.stderr)

    if args.summary:
        for name, cmds in data.items():
            hist = collections.Counter(c[1] for c in cmds)
            print(f"{name}: {len(cmds)} append sites")
            print("  opcodes:", dict(sorted(hist.items(), key=lambda kv: kv[0] if isinstance(kv[0], int) else 999)))
            subs = [c for c in cmds if c[1] == 11]
            if subs:
                print("  0xb sub-commands:", [c[2] for c in subs])
        return

    with open(args.output, "w", encoding="utf-8") as f:
        f.write("# Static append-site inventory from the user's TSW.exe.\n")
        f.write("# Not control-flow-aware; do not redistribute.\n")
        f.write("# floor\taddress\topcode\targ1\targ2\n")
        for name, cmds in data.items():
            for addr, op, a1, a2 in cmds:
                f.write(f"{name}\t{addr:08x}\t{op}\t{a1}\t{a2}\n")
    print(f"wrote {args.output}")


if __name__ == "__main__":
    main()
