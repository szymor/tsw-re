#!/usr/bin/env python3
"""Integration tests for the trace-free static timeline composer.

These tests need the original TSW.exe and objdump.  They lift the pilot
handlers into a temporary directory, compose the timelines statically, and
assert the exact command counts and that no builder effect is unresolved.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import compose_event_timeline as composer_mod  # noqa: E402
import event_snapshot  # noqa: E402

try:
    import unicorn  # noqa: F401
    HAVE_UNICORN = True
except ImportError:
    HAVE_UNICORN = False

import native_event_oracle  # noqa: E402

EXE = Path.home() / (
    ".wine/drive_c/Program Files (x86)/Tower of the Sorcerer/TSW.exe")

METHODS = {
    "floor10": "floor10",
    "doorstackin": "doorstackin",
    "roujin": "roujin",
    "zyouout": "zyouout",
    "zenolivework": "zenolivework",
    "openning2": "openning2",
    "madoushilivework": "madoushilivework",
    "handan": "handan",
    "mevent": "mevent",
}


def snapshot(floor: int, x: int, y: int, flag_index: int = 0x62,
             tile: tuple[int, int] | None = None,
             target_index: int = 0,
             flag_value: int = 0,
             sound_state: int = 0,
             extra_state: dict[int, int] | None = None,
             aux_overrides: dict[str, int] | None = None
             ) -> event_snapshot.Snapshot:
    state = [0] * event_snapshot.STATE_INT_COUNT
    state[0] = 1000
    state[1] = 100
    state[2] = 100
    state[0x010 // 4] = floor
    state[0x018 // 4] = x
    state[0x01C // 4] = y
    state[0x160 // 4] = 4  # facing used by the opening/roujin selectors
    state[0x168 // 4] = sound_state
    state[flag_index] = flag_value
    for index, value in (extra_state or {}).items():
        state[index] = value
    map_block = bytearray(event_snapshot.MAP_BLOCK)
    if tile is not None:
        index, value = tile
        map_block[2 + floor * 123 + index] = value
    aux = dict(event_snapshot.AUX_DEFAULTS)
    aux.update({"floor": floor, "x": x, "y": y, "target_index": target_index})
    aux.update(aux_overrides or {})
    return event_snapshot.Snapshot(state, bytes(map_block), aux=aux,
                                   source="composer-test")


@unittest.skipUnless(EXE.exists() and shutil.which("objdump"),
                     "original executable or objdump not present")
class ComposerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.tmp.name)
        for directory, method in METHODS.items():
            out = cls.root / directory
            out.mkdir(parents=True)
            subprocess.run(
                [sys.executable, str(Path(__file__).with_name(
                    "lift_event_handler.py")), str(EXE),
                 "--method", method, "--dir", str(out)],
                check=True, stdout=subprocess.DEVNULL)
            subprocess.run(
                [sys.executable, str(Path(__file__).with_name(
                    "build_event_ir.py")), str(out)],
                check=True, stdout=subprocess.DEVNULL)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def compose(self, method: str, snap: event_snapshot.Snapshot,
                entry: int):
        ir = __import__("json").loads(
            (self.root / method / "builder_ir.json").read_text(encoding="utf-8"))
        composer = composer_mod.Composer(
            composer_mod.load_callees(self.root), EXE, snap.map_block)
        state = list(snap.state)
        aux = snap.aux_with_derived()
        composer.walk(ir, entry, state, aux, {})
        composer.finalize()
        composer.state = state
        composer.aux = aux
        return composer

    def test_floor10(self):
        c = self.compose("floor10", snapshot(10, 5, 10, 0x6A), 0x46CBC7)
        self.assertEqual(len(c.commands), 462)
        self.assertEqual(c.unresolved, [])

    def test_roujin_floor2(self):
        c = self.compose("roujin", snapshot(2, 2, 7, 0x62), 0x449708)
        self.assertEqual(len(c.commands), 6)
        self.assertEqual(c.unresolved, [])
        self.assertEqual(
            [[v["opcode"], v["arg1"], v["arg2"]] for v in c.commands[:6]],
            [[0, 0, 0], [2, 0, 0], [0, 0, 0],
             [1, 2, 0], [0, 0, 0], [0, 0, 0]])

    def test_zenolivework(self):
        c = self.compose("zenolivework", snapshot(3, 5, 10), 0x461A0C)
        self.assertEqual(len(c.commands), 36)
        self.assertEqual(c.unresolved, [])

    def test_openning2(self):
        c = self.compose("openning2", snapshot(1, 5, 10), 0x464048)
        self.assertEqual(len(c.commands), 651)
        self.assertEqual(c.unresolved, [])

    def test_floor3_handan_trigger(self):
        snap = snapshot(3, 4, 8, tile=(92, 0x07), target_index=92)
        c = self.compose("handan", snap, 0x444490)
        self.assertEqual(len(c.commands), 41)
        self.assertEqual(c.unresolved, [])
        self.assertEqual(
            [c.commands[0]["opcode"], c.commands[0]["arg1"],
             c.commands[0]["arg2"]], [10, 10, 0])
        self.assertEqual(c.map_block[2 + 3 * 123 + 92], 0x06)

    def test_unresolved_switch_and_missing_target_are_reported(self):
        state = [0] * event_snapshot.STATE_INT_COUNT
        aux = dict(event_snapshot.AUX_DEFAULTS)
        for successors, reason in [
                ("unresolved-switch", "unresolved_switch"),
                ("0x00500000:jump", "missing_target")]:
            ir = {"blocks": [{"block_start": "0x00401000",
                              "successors": successors}],
                  "appends": [], "ops": []}
            composer = composer_mod.Composer({}, EXE, bytes(6273))
            composer.walk(ir, 0x401000, state, aux, {})
            self.assertTrue(any(u["reason"] == reason
                                for u in composer.unresolved),
                            composer.unresolved)

    def test_contract_paths_validate_successors(self):
        state = [0] * event_snapshot.STATE_INT_COUNT
        aux = dict(event_snapshot.AUX_DEFAULTS)
        cases = [
            (0x464141, "unresolved-switch", "unresolved_switch"),
            (0x464141, "0x00500000:fallthrough", "missing_target"),
            (0x464048, "0x00500000:fallthrough", "missing_target"),
        ]
        for start, successors, reason in cases:
            ir = {"blocks": [{"block_start": f"0x{start:08x}",
                              "successors": successors}],
                  "appends": [], "ops": []}
            composer = composer_mod.Composer({}, EXE, bytes(6273))
            composer.walk(ir, start, state, aux, {})
            self.assertTrue(any(u["reason"] == reason
                                for u in composer.unresolved),
                            (start, successors, composer.unresolved))

        # Valid computed destination present: malformed metadata and a
        # contradictory contract target must still be reported.
        ir = {"blocks": [
            {"block_start": "0x00464048", "successors": "garbage"},
            {"block_start": "0x0046410d", "successors": ""},
        ], "appends": [], "ops": []}
        composer = composer_mod.Composer({}, EXE, bytes(6273))
        composer.walk(ir, 0x464048, state, aux, {})
        self.assertTrue(any(u["reason"] == "invalid_successor"
                            for u in composer.unresolved), composer.unresolved)

        # Well-formed but contradictory successor metadata must also fail.
        ir = {"blocks": [
            {"block_start": "0x00464048",
             "successors": "0x00464122:fallthrough"},
            {"block_start": "0x0046410d", "successors": ""},
            {"block_start": "0x00464122", "successors": ""},
        ], "appends": [], "ops": []}
        composer = composer_mod.Composer({}, EXE, bytes(6273))
        composer.walk(ir, 0x464048, state, aux, {})
        self.assertTrue(any(u["reason"] == "contract_target_mismatch"
                            for u in composer.unresolved), composer.unresolved)

        # Loop contracts must not silently discard builder effects.
        ir = {"blocks": [{"block_start": "0x00464141", "successors": ""}],
              "appends": [],
              "ops": [{"block_start": "0x00464141",
                       "address": "0x00464141", "kind": "call",
                       "target": "0x00500000 unknown",
                       "instruction": "call 0x500000"}]}
        composer = composer_mod.Composer({}, EXE, bytes(6273))
        composer.walk(ir, 0x464141, state, aux, {})
        self.assertTrue(any(u["reason"] == "unsupported_loop_call"
                            for u in composer.unresolved), composer.unresolved)

        ir["ops"][0]["target"] = "0x004660d0 doorstackin"
        ir["ops"][0]["instruction"] = "call 0x4660d0"
        composer = composer_mod.Composer({}, EXE, bytes(6273))
        composer.walk(ir, 0x464141, state, aux, {})
        self.assertTrue(any(u["reason"] == "unsupported_loop_callee"
                            for u in composer.unresolved), composer.unresolved)

    def test_missing_callee_is_reported(self):
        state = [0] * event_snapshot.STATE_INT_COUNT
        aux = dict(event_snapshot.AUX_DEFAULTS)
        ir = {
            "blocks": [{"block_start": "0x00401000", "successors": ""}],
            "appends": [],
            "ops": [{"block_start": "0x00401000", "address": "0x00401000",
                     "kind": "call", "target": "0x004660d0 doorstackin",
                     "instruction": "call 0x4660d0"}],
        }
        composer = composer_mod.Composer({}, EXE, bytes(6273))
        composer.walk(ir, 0x401000, state, aux, {})
        self.assertTrue(any(u["reason"] == "missing_callee"
                            for u in composer.unresolved), composer.unresolved)

    def test_finalize_reports_unresolved_arguments(self):
        state = [0] * event_snapshot.STATE_INT_COUNT
        aux = dict(event_snapshot.AUX_DEFAULTS)
        ir = {
            "blocks": [{"block_start": "0x00401000", "successors": ""}],
            "appends": [{"block_start": "0x00401000", "opcode": 9,
                         "arg1": None, "arg2": 0,
                         "opcode_addr": "0x00401000"}],
            "ops": [],
        }
        composer = composer_mod.Composer({}, EXE, bytes(6273))
        composer.walk(ir, 0x401000, state, aux, {})
        composer.finalize()
        self.assertTrue(any(u["reason"] == "unresolved_argument"
                            for u in composer.unresolved), composer.unresolved)

    @unittest.skipUnless(HAVE_UNICORN, "unicorn is not installed")
    def test_static_native_same_input_parity(self):
        cases = [
            ("floor10", 0x46CBC7, 0x46CB1C, snapshot(10, 5, 10, 0x6A)),
            ("roujin", 0x449708, 0x449708, snapshot(2, 2, 7)),
            ("zenolivework", 0x461A0C, 0x461A0C, snapshot(3, 5, 10)),
            ("openning2", 0x464048, 0x464048, snapshot(1, 5, 10)),
            ("handan", 0x444490, 0x444490,
             snapshot(3, 4, 8, tile=(92, 0x07), target_index=92)),
            ("floor10_guard", 0x46CBC7, 0x46CB1C,
             snapshot(10, 5, 10, 0x6A, flag_value=1)),
            ("floor10_sound", 0x46CBC7, 0x46CB1C,
             snapshot(10, 5, 10, 0x6A, sound_state=1)),
            ("roujin_flag", 0x449708, 0x449708,
             snapshot(2, 2, 7, flag_value=1)),
            ("roujin_selector", 0x449708, 0x449708,
             snapshot(2, 2, 7, aux_overrides={"roujin_selector": 1})),
            ("roujin_floor3", 0x449708, 0x449708, snapshot(3, 9, 5)),
            ("roujin_floor4", 0x449708, 0x449708, snapshot(4, 9, 5)),
            ("roujin_floor6", 0x449708, 0x449708, snapshot(6, 9, 5)),
            ("roujin_floor3_repeat", 0x449708, 0x449708,
             snapshot(3, 9, 5, extra_state={0x43: 1})),
            ("roujin_floor16", 0x449708, 0x449708, snapshot(16, 9, 5)),
            ("roujin_floor16_gate", 0x449708, 0x449708,
             snapshot(16, 9, 5, extra_state={0x6D: 1})),
            ("roujin_floor16_off", 0x449708, 0x449708, snapshot(16, 8, 5)),
            ("handan_cleared", 0x444490, 0x444490,
             snapshot(3, 4, 8, tile=(92, 0x06), target_index=92)),
        ]
        for method, entry, native_entry, snap in cases:
            with self.subTest(method=method):
                ir_method = {
                    "floor10_guard": "floor10",
                    "floor10_sound": "floor10",
                    "roujin_flag": "roujin",
                    "roujin_selector": "roujin",
                    "roujin_floor3": "roujin",
                    "roujin_floor4": "roujin",
                    "roujin_floor6": "roujin",
                    "roujin_floor3_repeat": "roujin",
                    "roujin_floor16": "roujin",
                    "roujin_floor16_gate": "roujin",
                    "roujin_floor16_off": "roujin",
                    "handan_cleared": "handan",
                }.get(method, method)
                static = self.compose(ir_method, snap, entry)
                self.assertEqual(static.unresolved, [])
                aux = snap.aux_with_derived()
                harness = native_event_oracle.NativeHarness(
                    EXE, presentation_stubs=True)
                native = harness.call(
                    native_entry, list(snap.state), snap.map_block,
                    snap.state[0x010 // 4], aux)
                self.assertTrue(native.ok, native.error)
                self.assertEqual(
                    [[c["opcode"], c["arg1"], c["arg2"]]
                     for c in static.commands],
                    [list(c) for c in native.commands])
                native_state = [
                    int.from_bytes(native.state[i * 4:i * 4 + 4],
                                   "little", signed=True)
                    for i in range(event_snapshot.STATE_INT_COUNT)
                ]
                self.assertEqual(static.state, native_state)
                self.assertEqual(bytes(static.map_block), native.map_block)
                # The static composer treats idou (0x44c04c) as a call it
                # does not enter; its step loop leaves these behind.
                skipped = ({"event_selector", "event_sprite", "battle_turns"}
                           if method.startswith("handan") else set())
                for name in native_event_oracle.AUX_ADDRS:
                    if name in skipped:
                        continue
                    self.assertEqual(static.aux.get(name, 0),
                                     native.aux.get(name, 0))


if __name__ == "__main__":
    unittest.main()
