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
             target_index: int = 0) -> event_snapshot.Snapshot:
    state = [0] * event_snapshot.STATE_INT_COUNT
    state[0] = 1000
    state[1] = 100
    state[2] = 100
    state[0x010 // 4] = floor
    state[0x018 // 4] = x
    state[0x01C // 4] = y
    state[0x160 // 4] = 4  # facing used by the opening/roujin selectors
    state[flag_index] = 0
    map_block = bytearray(event_snapshot.MAP_BLOCK)
    if tile is not None:
        index, value = tile
        map_block[2 + floor * 123 + index] = value
    aux = dict(event_snapshot.AUX_DEFAULTS)
    aux.update({"floor": floor, "x": x, "y": y, "target_index": target_index})
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


if __name__ == "__main__":
    unittest.main()
