#!/usr/bin/env python3
"""Run the original event handlers through the minimal native harness.

This is the P3 native oracle runner.  It executes the original x86 handler code
with explicit presentation/runtime helper contracts and writes deterministic
JSON traces containing the command buffer, final state, map delta, auxiliary
global deltas and normalized stub invocations.

It is not a full VCL emulator: only the helpers with contracts in
`native_event_oracle.STUB_CONTRACTS` are stubbed.  Any unknown direct or
indirect transfer makes the run fail closed instead of producing a successful
trace.

Usage:
    /tmp/opencode/venv/bin/python docs/re/tools/run_native_oracle.py --case floor10
    /tmp/opencode/venv/bin/python docs/re/tools/run_native_oracle.py --all -d /tmp/native-traces
    /tmp/opencode/venv/bin/python docs/re/tools/run_native_oracle.py --snapshot /tmp/snapshot.json --case roujin
"""

from __future__ import annotations

import argparse
import base64
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


def load_snapshot(path: Path) -> event_snapshot.Snapshot:
    if path.suffix.lower() == ".json":
        return event_snapshot.Snapshot.from_manifest(
            json.loads(path.read_text(encoding="utf-8")))
    return event_snapshot.Snapshot.from_save(path)


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


def run_case(exe: Path, snapshot: event_snapshot.Snapshot | Path, name: str) -> dict:
    spec = CASES[name]
    if not isinstance(snapshot, event_snapshot.Snapshot):
        snapshot = load_snapshot(Path(snapshot))
    state = list(snapshot.state)
    state[0x010 // 4] = spec["floor"]
    state[0x018 // 4] = spec["x"]
    state[0x01C // 4] = spec["y"]
    state[spec["flag_index"]] = 0
    aux = snapshot.aux_with_derived()
    for aux_name in native_event_oracle.AUX_ADDRS:
        aux[aux_name] = int(aux.get(
            aux_name, event_snapshot.AUX_DEFAULTS.get(aux_name, 0)))
    aux["floor"] = spec["floor"]
    aux["x"] = spec["x"]
    aux["y"] = spec["y"]
    aux["sound_state"] = state[event_snapshot.STATE_VIEWS["sound_state"]]
    effective_inputs = {
        "state": state,
        "map_block_b64": base64.b64encode(snapshot.map_block).decode("ascii"),
        "aux": {aux_name: aux[aux_name]
                for aux_name in native_event_oracle.AUX_ADDRS},
        "floor": spec["floor"],
        "form_present": snapshot.form_present,
    }
    input_digest = hashlib.sha256(
        json.dumps(effective_inputs, sort_keys=True).encode("utf-8")).hexdigest()
    harness = native_event_oracle.NativeHarness(
        exe, presentation_stubs=True)
    result = harness.call(
        spec["entry"], state, snapshot.map_block, spec["floor"], aux)
    aux_initial = {aux_name: aux[aux_name]
                   for aux_name in native_event_oracle.AUX_ADDRS}
    aux_final = {aux_name: result.aux.get(aux_name, 0)
                 for aux_name in native_event_oracle.AUX_ADDRS}
    aux_delta = {
        aux_name: {"before": aux_initial[aux_name],
                   "after": aux_final[aux_name]}
        for aux_name in native_event_oracle.AUX_ADDRS
        if aux_initial[aux_name] != aux_final[aux_name]
    }
    invocations = result.stub_invocations
    return {
        "schema_version": 2,
        "method": name,
        "entry": f"0x{spec['entry']:08x}",
        "floor": spec["floor"],
        "ok": result.ok,
        "termination": result.termination,
        "tainted": result.tainted,
        "stub_mode": result.stub_mode,
        "error": result.error,
        "steps": result.steps,
        "stub_count": len(invocations),
        "stub_targets": sorted({int(inv["target"]) for inv in invocations}),
        "stub_invocations": invocations,
        "binary_sha256": result.digest,
        "input_digest": input_digest,
        "state_sha256": hashlib.sha256(
            b"".join((v & 0xFFFFFFFF).to_bytes(4, "little", signed=False)
                     for v in state)).hexdigest(),
        "map_sha256": hashlib.sha256(snapshot.map_block).hexdigest(),
        "commands": result.command_count,
        "command_truncated": result.command_truncated,
        "command_words": [list(cmd) for cmd in result.commands],
        "state_diff": state_diff(state, result.state),
        "map_diff": map_diff(snapshot.map_block, result.map_block),
        "map_final_sha256": hashlib.sha256(result.map_block).hexdigest(),
        "aux_initial": aux_initial,
        "aux_final": aux_final,
        "aux_diff": aux_delta,
    }


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--case", choices=sorted(CASES))
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    ap.add_argument("--save", type=Path, default=DEFAULT_SAVE)
    ap.add_argument("--snapshot", type=Path,
                    help="canonical snapshot manifest JSON (overrides --save)")
    ap.add_argument("-d", "--output-dir", type=Path)
    ap.add_argument("-o", "--output", type=Path)
    args = ap.parse_args()

    if bool(args.case) == bool(args.all):
        ap.error("choose exactly one of --case or --all")
    if not args.exe.exists():
        ap.error(f"executable not found: {args.exe}")
    snapshot_path = args.snapshot or args.save
    if not snapshot_path.exists():
        ap.error(f"snapshot not found: {snapshot_path}")

    snapshot = load_snapshot(snapshot_path)
    cases = sorted(CASES) if args.all else [args.case]
    traces = {}
    for name in cases:
        trace = run_case(args.exe, snapshot, name)
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
