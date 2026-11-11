#!/usr/bin/env python3
"""Unit tests for the event-oracle snapshot model."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import event_snapshot as snap  # noqa: E402
import read_save  # noqa: E402


def valid_state() -> list[int]:
    state = [0] * snap.STATE_INT_COUNT
    state[0] = 1000
    state[1] = 100
    state[2] = 100
    state[0x010 // 4] = 3
    state[0x018 // 4] = 2
    state[0x01C // 4] = 6
    total, alt = read_save.checksum(state)
    state[0x290 // 4] = total
    state[0x294 // 4] = alt
    state[0x2A8 // 4] = 12
    return state


class SnapshotTests(unittest.TestCase):
    def test_valid_snapshot_and_derived_aux(self):
        s = snap.Snapshot(valid_state(), bytes(snap.MAP_BLOCK))
        self.assertTrue(s.validate()["checksum_ok"])
        aux = s.aux_with_derived()
        self.assertEqual(aux["floor"], 3)
        self.assertEqual(aux["x"], 2)
        self.assertEqual(aux["y"], 6)
        self.assertEqual(aux["life"], 1000)
        self.assertEqual(len(s.floor_tiles(3)), 121)

    def test_wrong_sizes_are_rejected(self):
        with self.assertRaises(ValueError):
            snap.Snapshot([0] * 10, bytes(snap.MAP_BLOCK))
        with self.assertRaises(ValueError):
            snap.Snapshot(valid_state(), b"short")

    def test_manifest_contains_form_contract(self):
        s = snap.Snapshot(valid_state(), bytes(snap.MAP_BLOCK))
        manifest = s.to_manifest(floor=3)
        self.assertIn("form_fields", manifest)
        self.assertIn("map_display", manifest["form_fields"])
        self.assertEqual(len(manifest["tiles"]), 121)


if __name__ == "__main__":
    unittest.main()
