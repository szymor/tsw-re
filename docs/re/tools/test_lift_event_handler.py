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

    def test_block_start_for(self):
        blocks = [(0, 10), (11, 20)]
        self.assertEqual(lifter.block_start_for(blocks, 5), 0)
        self.assertEqual(lifter.block_start_for(blocks, 15), 11)


if __name__ == "__main__":
    unittest.main()
