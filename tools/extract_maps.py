#!/usr/bin/env python3
"""Write the 6273-byte map block used by the reimplementation.

Input is either an original saveN.dat (first block) or an extracted initial
map block.  Generated data goes to data/maps/ and is ignored by Git.
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
    parser.add_argument("-o", "--out", default="data/maps/maps.bin",
                        help="output file (default: data/maps/maps.bin)")
    args = parser.parse_args()

    path = Path(args.save or args.map_block)
    raw = path.read_bytes()
    if len(raw) < MAP_BLOCK_SIZE:
        sys.exit(f"{path} is too short: {len(raw)} < {MAP_BLOCK_SIZE}")
    block = raw[:MAP_BLOCK_SIZE]
    if block[:2] != HEADER:
        sys.exit(f"{path} has unexpected map header: {block[:2].hex()}")

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(block)
    print(f"wrote {out} ({len(block)} bytes)")
    print(f"floor 1 stair pair: {block[123]:02x} {block[124]:02x}")
    print(f"floor 1 map starts at offset 125")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
