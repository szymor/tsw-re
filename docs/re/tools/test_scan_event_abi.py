#!/usr/bin/env python3
"""Unit tests for the event ABI call-site inventory helpers."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import scan_event_abi as abi  # noqa: E402


class AbiTests(unittest.TestCase):
    def test_previous_writes_ignores_comparisons(self):
        insns = [
            (0x1000, "mov eax,ebx"),
            (0x1005, "cmp edx,0x3"),
            (0x100A, "call 0x2000"),
        ]
        writes = abi.previous_writes(insns, 2)
        self.assertEqual(writes["eax"], "mov eax,ebx")
        self.assertIsNone(writes["edx"])

    def test_previous_writes_sees_xor_zero(self):
        insns = [
            (0x1000, "xor edx,edx"),
            (0x1005, "call 0x2000"),
        ]
        writes = abi.previous_writes(insns, 1)
        self.assertEqual(writes["edx"], "xor edx,edx")


if __name__ == "__main__":
    unittest.main()
