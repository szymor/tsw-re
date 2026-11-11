#!/usr/bin/env python3
"""Write a 6273-byte map block for local testing.

A pristine extracted initial-map block is required for New Game and is written
to data/maps/initial.bin by default.  A map copied from a progressed save is
debug data only; it is written to data/maps/save-map.bin and must not be used
as the New Game map.  Generated data is ignored by Git.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

MAP_BLOCK_SIZE = 6273
HEADER = b"\x00\x01"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    src = parser.add_mutually_exclusive_group(required=True)
    src.add_argument("--save", help="original saveN.dat file")
    src.add_argument("--map-block", help="extracted map block file")
    parser.add_argument("-o", "--out", default=None,
                        help="output file (default depends on the source)")
    args = parser.parse_args()

    path = Path(args.save or args.map_block)
    raw = path.read_bytes()
    if len(raw) < MAP_BLOCK_SIZE:
        sys.exit(f"{path} is too short: {len(raw)} < {MAP_BLOCK_SIZE}")
    block = raw[:MAP_BLOCK_SIZE]
    if block[:2] != HEADER:
        sys.exit(f"{path} has unexpected map header: {block[:2].hex()}")

    if args.out:
        out = Path(args.out)
    elif args.map_block:
        out = Path("data/maps/initial.bin")
    else:
        out = Path("data/maps/save-map.bin")
        print("warning: save-derived maps are for inspection; New Game uses initial.bin")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(block)
    print(f"wrote {out} ({len(block)} bytes)")
    print(f"floor 1 stair pair: {block[123]:02x} {block[124]:02x}")
    print(f"floor 1 map starts at offset 125")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
