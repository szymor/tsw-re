#!/usr/bin/env python3
"""Deterministic event-oracle snapshot model.

The native oracle must start from a snapshot that contains more than the
serialized 684-byte state block: the 6273-byte map block plus the auxiliary
globals used by movement and event dispatch.  This module defines that
snapshot in one place so the P3 oracle has a reproducible input format.

It can read an original save file for convenience, but the snapshot JSON is
the canonical oracle input.  Unknown form/VCL fields are intentionally not
invented; the oracle contract must fail closed when a handler needs one.

Usage:
    python3 docs/re/tools/event_snapshot.py save1.dat --floor 10 -o snapshot.json
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

import read_save

MAP_BLOCK = read_save.MAP_BLOCK
STATE_BLOCK = read_save.STATE_BLOCK
STATE_INT_COUNT = 171

# Auxiliary globals used by the event trigger/VM path.  These are not part of
# the serialized state block.
AUX_DEFAULTS = {
    "target_index": 0,       # 0x48c584
    "direction": 4,          # 0x48c590, 4=up
    "sprite": 0,             # 0x48c58c
    "ui_mode": 0,            # 0x48c5ce
    "message_selector": 0,   # 0x48c5a0
    "event_selector": 0,     # 0x48c55c
    "current_monster": 0,    # 0x48c5b8
    "monster_hp": 0,         # 0x48c5bc
    "battle_flag": 0,        # 0x4b86b8
    "sound_state": 0,        # 0x4b87f0
    "facing": 4,             # 0x4b87e8
    "render_order_base": 0,  # 0x489b1f + i, see 02-map-format.md
}

# Presentation fields the native handlers read from the main form.  The oracle
# must provide typed stubs for these; exact VCL types are recovered in the P3
# harness.  Listing them here prevents accidental "zero-filled object" success.
FORM_FIELDS = {
    "map_display": 0x95,       # param_1[0x95]: +0x2c width, +0x30 height
    "map_image": 0x6c,
    "message_panel": 0x6d,
    "image_panel": 0x6e,
    "media_player": 0x111,
    "media_aux": 0x11b,
    "sound_a": 0xb5,
    "sound_b": 0xb6,
    "sound_c": 0x118,
    "sound_d": 0x119,
    "sound_e": 0x11a,
    "message_image": 0x106,
}


@dataclass
class Snapshot:
    state: list[int]
    map_block: bytes
    aux: dict[str, int] = field(default_factory=lambda: dict(AUX_DEFAULTS))
    form_present: bool = False
    source: str = "synthetic"

    def __post_init__(self) -> None:
        if len(self.state) != STATE_INT_COUNT:
            raise ValueError(f"state must have {STATE_INT_COUNT} ints")
        if len(self.map_block) != MAP_BLOCK:
            raise ValueError(f"map block must be {MAP_BLOCK} bytes")

    @classmethod
    def from_save(cls, path: Path) -> "Snapshot":
        data, map_block, state = read_save.parse(str(path))
        return cls(list(state), map_block, source=str(path))

    def floor_tiles(self, floor: int) -> list[int]:
        return list(read_save.floor_map(self.map_block, floor))

    def aux_with_derived(self) -> dict[str, int]:
        aux = dict(self.aux)
        aux["floor"] = self.state[0x010 // 4]
        aux["x"] = self.state[0x018 // 4]
        aux["y"] = self.state[0x01c // 4]
        aux["life"] = self.state[0]
        aux["attack"] = self.state[1]
        aux["defense"] = self.state[2]
        return aux

    def validate(self) -> dict[str, object]:
        total, alt, stored_sum, stored_alt, ok, note = read_save.check_state(self.state)
        return {
            "state_ints": len(self.state),
            "map_bytes": len(self.map_block),
            "checksum_ok": ok,
            "checksum_note": note,
            "computed_sum": total,
            "computed_alt": alt,
            "stored_sum": stored_sum,
            "stored_alt": stored_alt,
        }

    def to_manifest(self, floor: int | None = None) -> dict[str, object]:
        out: dict[str, object] = {
            "source": self.source,
            "state": self.state,
            "aux": self.aux_with_derived(),
            "form_present": self.form_present,
            "form_fields": FORM_FIELDS,
            "validation": self.validate(),
        }
        if floor is not None:
            out["floor"] = floor
            out["tiles"] = self.floor_tiles(floor)
        return out


def write_snapshot(path: Path, snapshot: Snapshot, floor: int | None = None) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(snapshot.to_manifest(floor), indent=2) + "\n",
                    encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("save", type=Path)
    ap.add_argument("--floor", type=int)
    ap.add_argument("-o", "--output", type=Path)
    args = ap.parse_args()

    snap = Snapshot.from_save(args.save)
    validation = snap.validate()
    if args.output:
        write_snapshot(args.output, snap, args.floor)
        print(f"wrote {args.output}")
    else:
        print(json.dumps({
            "source": snap.source,
            "validation": validation,
            "aux": snap.aux_with_derived(),
        }, indent=2))
    return 0 if validation["checksum_ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
