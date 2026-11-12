#!/usr/bin/env python3
"""Optional tests for the experimental native event harness.

These tests require the `unicorn` Python package.  They are skipped in the
normal system Python because the analysis venv is optional.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

try:
    import unicorn  # noqa: F401
    HAVE_UNICORN = True
except ImportError:
    HAVE_UNICORN = False

import native_event_oracle as native  # noqa: E402
import run_native_oracle  # noqa: E402

EXPECTED_COMMANDS = {
    "floor10": 462,
    "roujin": 6,
    "openning2": 609,
    "zenolivework": 36,
}


@unittest.skipUnless(HAVE_UNICORN, "unicorn is not installed")
class NativeHarnessTests(unittest.TestCase):
    def test_syokidata_self_test(self):
        exe = Path.home() / (
            ".wine/drive_c/Program Files (x86)/Tower of the Sorcerer/TSW.exe")
        if not exe.exists():
            self.skipTest("original TSW.exe not present")
        harness = native.NativeHarness(exe)
        state = [0] * 171
        state[0] = 1000
        state[1] = 100
        state[2] = 100
        state[0x010 // 4] = 1
        result = harness.call(0x455CB8, state, bytes(6273), 1)
        self.assertTrue(result.ok, result.error)
        self.assertEqual(result.termination, "returned")
        self.assertFalse(result.tainted)
        self.assertGreater(result.steps, 0)
        self.assertEqual(len(result.map_block), 6273)

    def test_pilot_native_traces(self):
        exe = run_native_oracle.DEFAULT_EXE
        save = run_native_oracle.DEFAULT_SAVE
        if not exe.exists() or not save.exists():
            self.skipTest("original executable/save not present")
        for name, expected in EXPECTED_COMMANDS.items():
            with self.subTest(name=name):
                trace = run_native_oracle.run_case(exe, save, name)
                self.assertTrue(trace["ok"], trace["error"])
                self.assertEqual(trace["termination"], "returned")
                self.assertFalse(trace["tainted"])
                self.assertFalse(trace["command_truncated"])
                self.assertEqual(trace["commands"], expected)
        roujin = run_native_oracle.run_case(exe, save, "roujin")
        self.assertEqual(
            roujin["command_words"][:6],
            [[0, 0, 0], [2, 0, 0], [0, 0, 0],
             [1, 2, 0], [0, 0, 0], [0, 0, 0]])


if __name__ == "__main__":
    unittest.main()
