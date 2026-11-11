#!/usr/bin/env python3
"""Unit tests for the append-site lifter helpers."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import lift_event_handler as lifter  # noqa: E402


class LifterTests(unittest.TestCase):
    def test_parse_immediate_and_register(self):
        self.assertEqual(lifter.parse_value("mov WORD PTR [esi],0x1"), 1)
        self.assertIsNone(lifter.parse_value("mov WORD PTR [esi],dx"))
        self.assertEqual(lifter.register_source("mov WORD PTR [esi],dx"), "dx")

    def test_resolve_register_immediate(self):
        insns = [
            (0x1000, "mov dx,0x28"),
            (0x1005, "mov WORD PTR [esi],dx"),
        ]
        self.assertEqual(lifter.resolve_register(insns, 1, "dx"), 0x28)

    def test_register_resolution_rejects_non_writes_and_partial_writes(self):
        self.assertIsNone(lifter.resolve_register(
            [(0x1000, "cmp dx,0x3"), (0x1005, "mov WORD PTR [esi],dx")], 1, "dx"))
        self.assertIsNone(lifter.resolve_register(
            [(0x1000, "add dx,0x3"), (0x1005, "mov WORD PTR [esi],dx")], 1, "dx"))
        self.assertIsNone(lifter.resolve_register(
            [(0x1000, "mov dh,0x12"), (0x1005, "mov WORD PTR [esi],dx")], 1, "dx"))
        self.assertEqual(lifter.resolve_register(
            [(0x1000, "mov dx,0x12345"), (0x1005, "mov WORD PTR [esi],dx")], 1, "dx"),
            0x2345)
        self.assertEqual(lifter.resolve_register(
            [(0x1000, "xor edx,edx"), (0x1005, "mov WORD PTR [esi],dx")], 1, "dx"), 0)
        self.assertIsNone(lifter.resolve_register(
            [(0x1000, "mov dx,0x1234"), (0x1005, "xor dl,dl"),
             (0x100A, "mov WORD PTR [esi],dx")], 2, "dx"))

    def test_block_start_for(self):
        blocks = [(0, 10), (11, 20)]
        self.assertEqual(lifter.block_start_for(blocks, 5), 0)
        self.assertEqual(lifter.block_start_for(blocks, 15), 11)


if __name__ == "__main__":
    unittest.main()
