#!/usr/bin/env python3
"""Run the original event handlers through the minimal native harness.

This is the P3 native oracle runner.  It executes the original x86 handler code
with typed presentation stubs and writes deterministic JSON traces containing
the command buffer, final state, map delta and harness diagnostics.

It is not a full VCL emulator: only the classified presentation/runtime helpers
listed in `native_event_oracle.py` are stubbed.  Any unknown call or transfer
makes the run fail closed instead of producing a successful trace.

Usage:
    /tmp/opencode/venv/bin/python docs/re/tools/run_native_oracle.py --case floor10
    /tmp/opencode/venv/bin/python docs/re/tools/run_native_oracle.py --all -d /tmp/native-traces
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

import event_snapshot
import native_event_oracle

DEFAULT_EXE = Path.home() / (
    ".wine/drive_c/Program Files (x86)/Tower of the Sorcerer/TSW.exe")
DEFAULT_SAVE = Path.home() / (
    ".wine/drive_c/Program Files (x86)/Tower of the Sorcerer/Savedat/save1.dat")

CASES = {
    "floor10": {
        "entry": 0x46CB1C,
        "floor": 10,
        "x": 5,
        "y": 10,
        "flag_index": 0x6A,
    },
    "roujin": {
        "entry": 0x449708,
        "floor": 2,
        "x": 2,
        "y": 7,
        "flag_index": 0x62,
    },
    "openning2": {
        "entry": 0x464048,
        "floor": 1,
        "x": 5,
        "y": 10,
        "flag_index": 0x62,
    },
    "zenolivework": {
        "entry": 0x461A0C,
        "floor": 3,
        "x": 5,
        "y": 10,
        "flag_index": 0x62,
    },
}


def state_diff(initial: list[int], final: bytes) -> list[dict[str, int]]:
    out = []
    for index in range(event_snapshot.STATE_INT_COUNT):
        before = initial[index]
        after = int.from_bytes(final[index * 4:index * 4 + 4], "little",
                               signed=True)
        if before != after:
            out.append({"index": index, "before": before, "after": after})
    return out


def map_diff(initial: bytes, final: bytes) -> list[dict[str, int]]:
    return [
        {"index": i, "before": before, "after": after}
        for i, (before, after) in enumerate(zip(initial, final))
        if before != after
    ]


def run_case(exe: Path, save: Path, name: str) -> dict:
    spec = CASES[name]
    snapshot = event_snapshot.Snapshot.from_save(save)
    state = list(snapshot.state)
    state[0x010 // 4] = spec["floor"]
    state[0x018 // 4] = spec["x"]
    state[0x01C // 4] = spec["y"]
    state[spec["flag_index"]] = 0
    aux = {
        "floor": spec["floor"],
        "x": spec["x"],
        "y": spec["y"],
        "sound_state": state[event_snapshot.STATE_VIEWS["sound_state"]],
    }
    harness = native_event_oracle.NativeHarness(
        exe, presentation_stubs=True)
    result = harness.call(
        spec["entry"], state, snapshot.map_block, spec["floor"], aux)
    manifest = snapshot.to_manifest(spec["floor"])
    snapshot_digest = hashlib.sha256(
        json.dumps(manifest, sort_keys=True).encode("utf-8")).hexdigest()
    return {
        "schema_version": 1,
        "method": name,
        "entry": f"0x{spec['entry']:08x}",
        "floor": spec["floor"],
        "ok": result.ok,
        "termination": result.termination,
        "tainted": result.tainted,
        "stub_mode": result.stub_mode,
        "error": result.error,
        "steps": result.steps,
        "stub_calls": [f"0x{addr:08x}" for addr in result.stub_calls],
        "stub_count": len(result.stub_calls),
        "binary_sha256": result.digest,
        "snapshot_digest": snapshot_digest,
        "commands": result.command_count,
        "command_truncated": result.command_truncated,
        "command_words": [list(cmd) for cmd in result.commands],
        "state_diff": state_diff(state, result.state),
        "map_diff": map_diff(snapshot.map_block, result.map_block),
        "map_sha256": hashlib.sha256(result.map_block).hexdigest(),
    }


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--case", choices=sorted(CASES))
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    ap.add_argument("--save", type=Path, default=DEFAULT_SAVE)
    ap.add_argument("-d", "--output-dir", type=Path)
    ap.add_argument("-o", "--output", type=Path)
    args = ap.parse_args()

    if bool(args.case) == bool(args.all):
        ap.error("choose exactly one of --case or --all")
    if not args.exe.exists():
        ap.error(f"executable not found: {args.exe}")
    if not args.save.exists():
        ap.error(f"save snapshot not found: {args.save}")

    cases = sorted(CASES) if args.all else [args.case]
    traces = {}
    for name in cases:
        trace = run_case(args.exe, args.save, name)
        traces[name] = trace
        print(f"{name}: ok={trace['ok']} commands={trace['commands']} "
              f"termination={trace['termination']} stubs={trace['stub_count']}")
        if args.output_dir:
            args.output_dir.mkdir(parents=True, exist_ok=True)
            path = args.output_dir / f"{name}.json"
            path.write_text(json.dumps(trace, indent=2) + "\n",
                            encoding="utf-8")
    if args.output:
        args.output.write_text(json.dumps(traces, indent=2) + "\n",
                               encoding="utf-8")
    return 0 if all(t["ok"] for t in traces.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
