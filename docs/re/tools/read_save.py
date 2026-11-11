#!/usr/bin/env python3
"""Read a Tower of the Sorcerer v1.2 save file.

Usage:
    read_save.py save1.dat
    read_save.py save1.dat --floor 1 --ppm floor1.ppm
    read_save.py save1.dat --state

The script only reads the user's own files.  It does not contain game data.
"""

import argparse
import struct
import sys

MAP_BLOCK = 6273
STATE_BLOCK = 684
STATE_BASE = 0x4B8688

FIELDS = [
    (0x000, "life"),
    (0x004, "attack"),
    (0x008, "defense"),
    (0x00C, "gold"),
    (0x010, "floor"),
    (0x014, "floor_limit"),
    (0x018, "x"),
    (0x01C, "y"),
    (0x020, "yellow_keys"),
    (0x024, "red_keys"),
    (0x028, "blue_keys"),
    (0x02C, "unknown_02c"),
    (0x030, "battle_flag"),
    (0x03C, "weapon_level"),
    (0x040, "shield_level"),
    (0x274, "save_magic"),
    (0x27C, "option_a"),
    (0x280, "option_b"),
    (0x290, "checksum_sum"),
    (0x294, "checksum_alt"),
    (0x2A8, "save_version"),
]


def checksum(ints):
    total = 0
    alt = 0
    for i, v in enumerate(ints[:164]):
        total += v
        alt += -v if (i & 1) else v
    return total, alt


def parse(path):
    data = open(path, "rb").read()
    if len(data) < MAP_BLOCK + STATE_BLOCK:
        raise SystemExit(f"{path}: expected at least {MAP_BLOCK+STATE_BLOCK} bytes, got {len(data)}")
    map_block = data[:MAP_BLOCK]
    state = struct.unpack("<171i", data[MAP_BLOCK:MAP_BLOCK + STATE_BLOCK])
    return data, map_block, state


def floor_map(map_block, floor):
    # header (2) + floor*123; each record is 121 map bytes + 2 gap bytes
    off = 2 + floor * 123
    return map_block[off:off + 121]


def print_state(state):
    for off, name in FIELDS:
        v = state[off // 4]
        print(f"0x{off:03x} {name:16s} {v:12d}  0x{v & 0xffffffff:08x}")
    print("special items:", state[0x44 // 4:0x44 // 4 + 15])
    print("sword flags:  ", state[0x80 // 4:0x80 // 4 + 5])
    print("shield flags: ", state[0x94 // 4:0x94 // 4 + 5])
    total, alt = checksum(state)
    print(f"checksum computed: sum={total} alt={alt}")
    print(f"checksum stored:   sum={state[0x290 // 4]} alt={state[0x294 // 4]}")


def write_ppm(path, tiles, scale=8):
    """Simple false-color PPM so no copyrighted art is needed."""
    palette = {
        1: (0, 0, 255), 2: (255, 0, 0), 3: (255, 255, 0),
        4: (0, 128, 255), 5: (255, 128, 0), 6: (80, 80, 80),
        7: (120, 80, 40), 8: (140, 90, 50), 9: (160, 100, 60),
        10: (0, 255, 0), 11: (0, 200, 0), 12: (200, 0, 200),
        13: (180, 180, 255),
    }
    with open(path, "wb") as f:
        f.write(f"P6\n{11*scale} {11*scale}\n255\n".encode())
        for y in range(11):
            for _ in range(scale):
                for x in range(11):
                    v = tiles[y * 11 + x]
                    r, g, b = palette.get(v, (0, 0, 0))
                    f.write(bytes((r, g, b)) * scale)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("save")
    ap.add_argument("--state", action="store_true", help="print the whole state summary")
    ap.add_argument("--floor", type=int, help="print a floor as a hex grid")
    ap.add_argument("--ppm", help="write a false-color PPM of the selected floor")
    args = ap.parse_args()

    data, map_block, state = parse(args.save)
    print(f"{args.save}: {len(data)} bytes")
    print_state(state) if args.state else None

    if args.floor is not None:
        if not 1 <= args.floor <= 50:
            raise SystemExit("floor must be 1..50")
        tiles = floor_map(map_block, args.floor)
        for y in range(11):
            print(" ".join(f"{tiles[y*11+x]:02x}" for x in range(11)))
        if args.ppm:
            write_ppm(args.ppm, tiles)
            print("wrote", args.ppm)


if __name__ == "__main__":
    main()
