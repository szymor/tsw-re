#!/usr/bin/env python3
"""Merge lifter outputs into a builder-IR JSON bundle.

Input is a directory produced by:

    lift_event_handler.py --method <name> --dir <dir>

It reads appends.tsv, blocks.tsv and ops.tsv and emits builder_ir.json.  The
bundle is the input of the synthetic oracle and the future native oracle.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

SCHEMA_VERSION = 1


def read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f, delimiter="\t"))


def parse_int(value: str) -> int | None:
    if value in ("", "?", "-"):
        return None
    return int(value, 16) if value.startswith("0x") else int(value)


def build_bundle(ir_dir: Path) -> dict[str, object]:
    appends = read_tsv(ir_dir / "appends.tsv")
    blocks = read_tsv(ir_dir / "blocks.tsv")
    ops = read_tsv(ir_dir / "ops.tsv")
    method = appends[0]["method"] if appends else "unknown"
    return {
        "schema_version": SCHEMA_VERSION,
        "method": method,
        "blocks": blocks,
        "appends": [
            {
                "seq": int(row["seq"]),
                "block_start": row["block_start"],
                "opcode": parse_int(row["opcode"]),
                "arg1": parse_int(row["arg1"]),
                "arg2": parse_int(row["arg2"]),
                "opcode_addr": row.get("opcode_addr", ""),
                "arg1_addr": row.get("arg1_addr", ""),
                "arg2_addr": row.get("arg2_addr", ""),
                "arg1_expr": row.get("arg1_expr", ""),
                "arg2_expr": row.get("arg2_expr", ""),
                "block_entry": row.get("block_entry", ""),
                "instruction": row["instruction"],
            }
            for row in appends
        ],
        "ops": ops,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ir_dir", type=Path)
    ap.add_argument("-o", "--output", type=Path)
    args = ap.parse_args()

    for name in ("appends.tsv", "blocks.tsv", "ops.tsv"):
        if not (args.ir_dir / name).exists():
            print(f"error: missing {args.ir_dir / name}", file=sys.stderr)
            return 2
    bundle = build_bundle(args.ir_dir)
    output = args.output or args.ir_dir / "builder_ir.json"
    output.write_text(json.dumps(bundle, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {output}")
    print(f"method: {bundle['method']}")
    print(f"appends: {len(bundle['appends'])} blocks: {len(bundle['blocks'])} "
          f"ops: {len(bundle['ops'])}")
    unresolved = sum(1 for a in bundle["appends"] if a["opcode"] is None)
    print(f"unresolved opcodes: {unresolved}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
