#!/usr/bin/env python3
"""Extract the initial map block from the user's own copy.

Two modes:

1. From a Ghidra decompilation of `syokidata` (0x455cb8):
       extract_initial_map.py --decomp 00455cb8_FUN_00455cb8.c -o initial_map.bin

   `syokidata` writes the 6271-byte map database as a long sequence of byte
   stores.  This script collects stores in the range 0x4b8936..0x4ba1b4,
   prepends the 00 01 header, and writes a 6273-byte map block.

2. From any original save file (the map block is the first 6273 bytes):
       extract_initial_map.py --save save1.dat -o map_block.bin

The script does not contain game data.
"""

import argparse
import re
import struct
import sys

START = 0x4B8936
END = 0x4BA1B4
SIZE = END - START + 1  # 6271


def from_decomp(path):
    text = open(path, "r", encoding="utf-8", errors="replace").read()
    pat = re.compile(r"DAT_(004b[0-9a-fA-F]{4})\s*=\s*([^;]+);")
    vals = {}
    for m in pat.finditer(text):
        addr = int(m.group(1), 16)
        if not (START <= addr <= END):
            continue
        expr = m.group(2).strip().replace("(byte)", "").strip()
        try:
            v = int(eval(expr, {"__builtins__": {}}, {})) & 0xFF
        except Exception:
            continue
        vals[addr] = v
    missing = [a for a in range(START, END + 1) if a not in vals]
    if missing:
        raise SystemExit(f"missing {len(missing)} bytes; first {missing[0]:#x}")
    return bytes(vals[a] for a in range(START, END + 1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--decomp", help="Ghidra decompilation of syokidata")
    ap.add_argument("--save", help="original save file (uses first 6273 bytes)")
    ap.add_argument("-o", "--output", default="initial_map.bin")
    args = ap.parse_args()

    if args.save:
        d = open(args.save, "rb").read()
        if len(d) < 6273:
            raise SystemExit("save file too short")
        block = d[:6273]
    elif args.decomp:
        block = b"\x00\x01" + from_decomp(args.decomp)
    else:
        raise SystemExit("provide --decomp or --save")

    open(args.output, "wb").write(block)
    print(f"wrote {args.output}: {len(block)} bytes")
    if len(block) != 6273:
        raise SystemExit("internal error: wrong block length")


if __name__ == "__main__":
    main()
