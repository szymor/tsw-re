#!/usr/bin/env python3
"""Unit tests for the trigger-graph extractor."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import scan_event_triggers as triggers  # noqa: E402


class TriggerTests(unittest.TestCase):
    def test_ichicheck_floor_row(self):
        tables = [{
            "table_id": "ichicheck_floor", "kind": "byte", "base": 0x46CB63,
            "jump": 0x46CB8B, "index": 10, "target": 0x46CBC7,
            "source": "ichicheck", "notes": "floors 10..49",
        }]
        rows = triggers.trigger_rows(tables, [(0x46CBC7, "floor10")])
        row = rows[0]
        self.assertEqual(row["floor"], 10)
        self.assertEqual(row["entry_point"], "ichicheck")
        self.assertEqual(row["handler"], "0x0046cbc7")
        self.assertEqual(row["handler_label"], "floor10@0046cbc7")

    def test_handan_target_labels(self):
        self.assertEqual(triggers.label_for([], 0x445363), "syounin")
        self.assertEqual(triggers.label_for([], 0x4447D2), "handan.case7_mevent")
        self.assertEqual(triggers.label_for([], 0x448FBC), "handan.return")

    def test_byte_target_resolution_and_bounds(self):
        self.assertEqual(
            triggers.resolve_byte_targets([0, 1, 1], [0x10, 0x20]),
            [0x10, 0x20, 0x20],
        )
        with self.assertRaises(ValueError):
            triggers.resolve_byte_targets([2], [0x10])

    def test_table_specs_declare_lengths(self):
        expected = {
            "handan_raw_00_18": (25, None),
            "handan_raw_20_32": (19, None),
            "handan_raw_33_3c": (10, None),
            "mevent_floor": (50, 17),
            "ichicheck_floor": (40, 15),
            "roujin_floor": (49, 19),
            "moncheck_floor": (51, 23),
        }
        for spec in triggers.TABLES:
            length, jump_length = expected[spec["id"]]
            self.assertEqual(spec["length"], length)
            if jump_length is not None:
                self.assertEqual(spec["jump_length"], jump_length)

    def test_pointer_entry_exact_length_and_bounds(self):
        data = bytearray(0x410)
        data[0x400:0x404] = (0x401100).to_bytes(4, "little")
        data[0x404:0x408] = (0x401200).to_bytes(4, "little")
        self.assertEqual(
            triggers.decode_pointer_entries(bytes(data), 0x401000, 2),
            [0x401100, 0x401200],
        )
        data[0x404:0x408] = (0x1000).to_bytes(4, "little")
        with self.assertRaises(ValueError):
            triggers.decode_pointer_entries(bytes(data), 0x401000, 2)

    def test_truncated_byte_table_is_rejected(self):
        with self.assertRaises(ValueError):
            triggers.load_byte_table(bytes(0x402), 0x401000, 5)

    def test_known_data_ranges_are_excluded(self):
        self.assertTrue(triggers.in_ranges(0x4658E3, [(0x4658D0, 0x465900)]))
        self.assertFalse(triggers.in_ranges(0x465900, [(0x4658D0, 0x465900)]))

    def test_decode_tables_applies_index_base(self):
        data = bytearray(0x420)
        data[0x400:0x402] = bytes([0, 1])
        data[0x410:0x414] = (0x402000).to_bytes(4, "little")
        data[0x414:0x418] = (0x402010).to_bytes(4, "little")
        spec = {
            "id": "synthetic_byte", "kind": "byte", "base": 0x401000,
            "jump": 0x401010, "jump_length": 2, "length": 2,
            "index_base": 10, "index_name": "floor", "source": "test",
            "notes": "synthetic index-base test",
        }
        old_tables = triggers.TABLES
        triggers.TABLES = [spec]
        try:
            with tempfile.NamedTemporaryFile() as tmp:
                tmp.write(data)
                tmp.flush()
                rows, _ = triggers.decode_tables(Path(tmp.name))
        finally:
            triggers.TABLES = old_tables
        self.assertEqual([r["index"] for r in rows], [10, 11])
        self.assertEqual([r["target"] for r in rows], [0x402000, 0x402010])


if __name__ == "__main__":
    unittest.main()
