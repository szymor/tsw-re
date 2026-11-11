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


if __name__ == "__main__":
    unittest.main()
