#!/usr/bin/env python3
"""Unit tests for builder-IR bundling and the synthetic oracle."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import build_event_ir as irbuild  # noqa: E402
import event_snapshot as snap  # noqa: E402
import run_synthetic_oracle as oracle  # noqa: E402


class IrTests(unittest.TestCase):
    def test_build_bundle_from_tsv(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            (d / "appends.tsv").write_text(
                "method\tblock_start\tseq\topcode\targ1\targ2\topcode_addr\t"
                "arg1_addr\targ2_addr\tinstruction\tblock_entry\n"
                "m@0x1000\t0x1000\t0\t0x14\t0x1\t0x2a\t0x1000\t0x1004\t0x1008\t"
                "mov\tentry\n", encoding="utf-8")
            (d / "blocks.tsv").write_text(
                "block_start\tblock_end\tlast_instruction\tpredicate\t"
                "predecessors\tsuccessors\n", encoding="utf-8")
            (d / "ops.tsv").write_text(
                "block_start\taddress\tkind\ttarget\tinstruction\n", encoding="utf-8")
            bundle = irbuild.build_bundle(d)
            self.assertEqual(bundle["method"], "m@0x1000")
            self.assertEqual(bundle["appends"][0]["opcode"], 0x14)
            self.assertEqual(bundle["appends"][0]["arg2"], 0x2a)

    def test_synthetic_oracle_executes_appends(self):
        manifest = snap.Snapshot([0] * snap.STATE_INT_COUNT,
                                 bytes(snap.MAP_BLOCK)).to_manifest()
        ir = {
            "method": "test",
            "blocks": [{"block_start": "0x1000", "successors": ""}],
            "appends": [
                {"block_start": "0x1000", "opcode": 0x14, "arg1": 1, "arg2": 42},
            ],
        }
        trace = oracle.run(ir, manifest, 0x1000)
        self.assertEqual(trace["steps"], 1)
        self.assertEqual(trace["state_diff"],
                         [{"index": 1, "before": 0, "after": 42}])

    def test_synthetic_oracle_compare(self):
        manifest = snap.Snapshot([0] * snap.STATE_INT_COUNT,
                                 bytes(snap.MAP_BLOCK)).to_manifest()
        ir = {"method": "test",
              "blocks": [{"block_start": "0x1000", "successors": ""}],
              "appends": [{"block_start": "0x1000", "opcode": 0x10,
                           "arg1": 5, "arg2": 0}]}
        a = oracle.run(ir, manifest, 0x1000)
        b = oracle.run(ir, manifest, 0x1000)
        self.assertTrue(oracle.compare(a, b))

    def test_synthetic_oracle_applies_builder_global_writes(self):
        manifest = snap.Snapshot([0] * snap.STATE_INT_COUNT,
                                 bytes(snap.MAP_BLOCK)).to_manifest()
        ir = {
            "method": "test",
            "blocks": [{"block_start": "0x1000", "successors": ""}],
            "appends": [],
            "ops": [{
                "block_start": "0x1000",
                "address": "0x1000",
                "kind": "write_global", "target": "ds:0x4b8810",
                "instruction": "mov DWORD PTR ds:0x4b8810,0x1",
            }],
        }
        trace = oracle.run(ir, manifest, 0x1000)
        self.assertEqual(trace["builder_writes"],
                         [{"kind": "state", "index": 0x62, "before": 0,
                           "after": 1, "address": "0x004b8810"}])

    def test_predicate_evaluation_uses_state_and_aux(self):
        state = [0] * snap.STATE_INT_COUNT
        state[0x6A] = 0
        aux = {"message_selector": 0x1B}
        self.assertTrue(oracle.eval_predicate(
            "DWORD PTR ds:0x4b8830 == 0x0", state, aux))
        self.assertTrue(oracle.eval_predicate(
            "DWORD PTR ds:0x48c5a0 == 0x1b", state, aux))
        self.assertIsNone(oracle.eval_predicate("eax == 0x0", state, aux))

    def test_walk_builder_takes_known_branch(self):
        state = [0] * snap.STATE_INT_COUNT
        state[0x6A] = 0
        ir = {
            "blocks": [
                {"block_start": "0x1000", "successors":
                 "0x1010:branch:(DWORD PTR ds:0x4b8830 == 0x0) | "
                 "0x1020:fallthrough:not(DWORD PTR ds:0x4b8830 == 0x0)"},
                {"block_start": "0x1010", "successors": ""},
                {"block_start": "0x1020", "successors": ""},
            ],
            "ops": [
                {"block_start": "0x1010", "address": "0x1010",
                 "kind": "write_global", "target": "ds:0x4b8688",
                 "instruction": "mov DWORD PTR ds:0x4b8688,0x1"},
                {"block_start": "0x1020", "address": "0x1020",
                 "kind": "write_global", "target": "ds:0x4b868c",
                 "instruction": "mov DWORD PTR ds:0x4b868c,0x2"},
            ],
        }
        writes, _unknown, _calls, _external, visited = oracle.walk_builder(
            ir, 0x1000, state, {})
        self.assertEqual(writes, [{"kind": "state", "index": 0,
                                   "before": 0, "after": 1,
                                   "address": "0x004b8688"}])
        self.assertEqual(visited, [0x1000, 0x1010])

    def test_appends_follow_visited_order(self):
        manifest = snap.Snapshot([0] * snap.STATE_INT_COUNT,
                                 bytes(snap.MAP_BLOCK)).to_manifest()
        ir = {
            "method": "test",
            "blocks": [
                {"block_start": "0x1000",
                 "successors": "0x3000:jump | 0x2000:jump"},
                {"block_start": "0x2000", "successors": ""},
                {"block_start": "0x3000", "successors": ""},
            ],
            "appends": [
                {"block_start": "0x2000", "seq": 0, "opcode": 0x10,
                 "arg1": 2, "arg2": 0},
                {"block_start": "0x3000", "seq": 1, "opcode": 0x10,
                 "arg1": 3, "arg2": 0},
            ],
        }
        trace = oracle.run(ir, manifest, 0x1000)
        self.assertEqual(trace["command_words"], [[0x10, 3, 0], [0x10, 2, 0]])

    def test_unclassified_call_and_unsupported_write_mark_incomplete(self):
        manifest = snap.Snapshot([0] * snap.STATE_INT_COUNT,
                                 bytes(snap.MAP_BLOCK)).to_manifest()
        ir = {
            "method": "test",
            "blocks": [{"block_start": "0x1000", "successors": ""}],
            "appends": [],
            "ops": [
                {"block_start": "0x1000", "address": "0x1000",
                 "kind": "call", "target": "unknown_fn", "instruction": "call 0x9000"},
                {"block_start": "0x1000", "address": "0x1004",
                 "kind": "write_global", "target": "ds:0x4b8688",
                 "instruction": "add DWORD PTR ds:0x4b8688,0x5"},
            ],
        }
        trace = oracle.run(ir, manifest, 0x1000)
        self.assertFalse(trace["complete"])
        self.assertTrue(any(e["kind"] == "unclassified_call"
                            for e in trace["builder_unresolved"]))
        self.assertTrue(any(e["kind"] == "unsupported_write"
                            for e in trace["builder_unresolved"]))

    def test_byte_predicate_uses_low_byte(self):
        state = [0] * snap.STATE_INT_COUNT
        state[0] = 0x100
        self.assertTrue(oracle.eval_predicate(
            "BYTE PTR ds:0x4b8688 == 0x0", state, {}))
        self.assertTrue(oracle.eval_predicate(
            "BYTE PTR ds:0x4b8689 == 0x1", state, {}))
        self.assertTrue(oracle.eval_predicate(
            "DWORD PTR ds:0x4b8688 == 0x100", state, {}))
        self.assertTrue(oracle.eval_predicate(
            "BYTE PTR ds:0x48c5a0 == 0x1b", state,
            {"message_selector": 0x1B}))


if __name__ == "__main__":
    unittest.main()
