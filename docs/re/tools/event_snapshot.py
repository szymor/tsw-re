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
import base64
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
    "direction": 1,          # 0x48c590, 1=up,2=left,3=right,4=down
    "sprite": 0,             # 0x48c58c
    "ui_mode": 0,            # 0x48c5ce
    "message_selector": 0,   # 0x48c5a0
    "event_selector": 0,     # 0x48c55c
    "event_sprite": 0,       # 0x48c558
    "event_param": 0,        # 0x48c560
    "roujin_aux": 0,         # 0x48c5a4
    "roujin_selector": 0,    # 0x48c564
    "ui_selector": 0,        # 0x48c5c8
    "current_monster": 0,    # 0x48c5b8
    "monster_hp": 0,         # 0x48c5bc
    "render_order_base": 0,  # 0x489b1f + i, see 02-map-format.md
}

AUX_ADDRS = {
    0x48C584: "target_index",
    0x48C590: "direction",
    0x48C58C: "sprite",
    0x48C5CE: "ui_mode",
    0x48C5A0: "message_selector",
    0x48C55C: "event_selector",
    0x48C558: "event_sprite",
    0x48C560: "event_param",
    0x48C5A4: "roujin_aux",
    0x48C564: "roujin_selector",
    0x48C5C8: "ui_selector",
    0x48C5B8: "current_monster",
    0x48C5BC: "monster_hp",
}

# Serialized-state aliases.  These are not independent auxiliary globals;
# they are named views into the state block.
STATE_VIEWS = {
    "battle_flag": 0x030 // 4,  # 0x4b86b8
    "facing": 0x160 // 4,       # 0x4b87e8
    "sound_state": 0x168 // 4,  # 0x4b87f0
}

# Presentation fields the native handlers read from the main form.  The values
# are int32/pointer *slot indices*, not byte offsets; the byte offset is
# slot*4.  Exact VCL types are recovered in the P3 harness.
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

FORM_FIELD_BYTES = {name: slot * 4 for name, slot in FORM_FIELDS.items()}


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
        for name, index in STATE_VIEWS.items():
            aux[name] = self.state[index]
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
            "map_block_b64": base64.b64encode(self.map_block).decode("ascii"),
            "map_block_len": len(self.map_block),
            "aux": self.aux_with_derived(),
            "form_present": self.form_present,
            "form_fields": FORM_FIELDS,
            "form_field_bytes": FORM_FIELD_BYTES,
            "validation": self.validate(),
        }
        if floor is not None:
            out["floor"] = floor
            out["tiles"] = self.floor_tiles(floor)
        return out

    @classmethod
    def from_manifest(cls, manifest: dict[str, object]) -> "Snapshot":
        state = [int(v) for v in manifest["state"]]  # type: ignore[index]
        map_block = base64.b64decode(str(manifest["map_block_b64"]))
        source = str(manifest.get("source", "manifest"))
        raw_aux = manifest.get("aux", {}) or {}
        allowed = set(AUX_DEFAULTS) | set(STATE_VIEWS) | {
            "floor", "x", "y", "life", "attack", "defense",
        }
        unknown = sorted(set(raw_aux) - allowed)
        if unknown:
            raise ValueError(f"unknown manifest auxiliary keys: {unknown}")
        aux = {name: int(raw_aux[name]) for name in AUX_DEFAULTS if name in raw_aux}
        # Derived state views may be present in the manifest; validate them
        # rather than storing duplicate inputs.
        derived = dict(STATE_VIEWS)
        derived.update({
            "floor": 0x010 // 4,
            "x": 0x018 // 4,
            "y": 0x01C // 4,
            "life": 0,
            "attack": 1,
            "defense": 2,
        })
        for name, index in derived.items():
            if name in raw_aux and int(raw_aux[name]) != state[index]:
                raise ValueError(
                    f"manifest auxiliary {name} conflicts with state[{index}]")
        return cls(state, map_block, aux=aux,
                   form_present=bool(manifest.get("form_present", False)),
                   source=source)


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
