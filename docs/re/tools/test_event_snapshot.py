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
        self.assertIn("map_block_b64", manifest)
        self.assertEqual(manifest["map_block_len"], snap.MAP_BLOCK)
        self.assertIn("form_fields", manifest)
        self.assertIn("map_display", manifest["form_fields"])
        self.assertEqual(manifest["form_field_bytes"]["map_display"], 0x95 * 4)
        self.assertEqual(len(manifest["tiles"]), 121)

    def test_manifest_round_trips_map_and_state(self):
        s = snap.Snapshot(valid_state(), bytes(i % 256 for i in range(snap.MAP_BLOCK)))
        manifest = s.to_manifest(floor=3)
        restored = snap.Snapshot.from_manifest(manifest)
        self.assertEqual(restored.state, s.state)
        self.assertEqual(restored.map_block, s.map_block)

    def test_manifest_round_trips_independent_aux_and_form(self):
        s = snap.Snapshot(valid_state(), bytes(snap.MAP_BLOCK))
        s.aux["direction"] = 3
        s.aux["target_index"] = 77
        s.aux["ui_mode"] = 2
        s.form_present = True
        restored = snap.Snapshot.from_manifest(s.to_manifest())
        self.assertEqual(restored.aux["direction"], 3)
        self.assertEqual(restored.aux["target_index"], 77)
        self.assertEqual(restored.aux["ui_mode"], 2)
        self.assertTrue(restored.form_present)

    def test_manifest_rejects_conflicting_derived_view(self):
        s = snap.Snapshot(valid_state(), bytes(snap.MAP_BLOCK))
        manifest = s.to_manifest()
        manifest["aux"]["facing"] = 1  # valid_state facing is 0
        with self.assertRaises(ValueError):
            snap.Snapshot.from_manifest(manifest)

    def test_state_views_are_derived_not_duplicated(self):
        state = valid_state()
        state[0x030 // 4] = 7
        state[0x160 // 4] = 4
        state[0x168 // 4] = 3
        s = snap.Snapshot(state, bytes(snap.MAP_BLOCK))
        aux = s.aux_with_derived()
        self.assertEqual(aux["battle_flag"], 7)
        self.assertEqual(aux["facing"], 4)
        self.assertEqual(aux["sound_state"], 3)
        self.assertNotIn("battle_flag", s.aux)
        self.assertNotIn("facing", s.aux)


if __name__ == "__main__":
    unittest.main()
