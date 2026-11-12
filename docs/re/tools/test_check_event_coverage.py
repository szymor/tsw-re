#!/usr/bin/env python3
"""Unit tests for the event-coverage helpers."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_event_coverage as coverage  # noqa: E402


def fixture():
    insns = [
        (0x1000, "mov eax,1"),
        (0x1002, "jne 0x1008"),
        (0x1004, "mov ebx,2"),
        (0x1006, "jmp 0x100c"),
        (0x1008, "mov ecx,3"),
        (0x100A, "mov edx,4"),
        (0x100C, "ret"),
    ]
    blocks = [(0, 1), (2, 3), (4, 5), (6, 6)]
    addr_to_block = {addr: bi for bi in range(len(blocks))
                     for addr, _ in insns[blocks[bi][0]:blocks[bi][1] + 1]}
    table_edges: dict[int, list[int]] = {}
    successors = coverage.build_successors(
        insns, blocks, addr_to_block, table_edges)
    return insns, blocks, addr_to_block, successors


class CoverageTests(unittest.TestCase):
    def test_branch_successors(self):
        _insns, _blocks, _map, successors = fixture()
        self.assertEqual(successors[0], [2, 1])
        self.assertEqual(successors[1], [3])
        self.assertEqual(successors[2], [3])
        self.assertEqual(successors[3], [])

    def test_runs_to_blocks_keeps_transfer_order(self):
        insns, blocks, mapping, _ = fixture()
        taken = [[0x1000, 0x1004], [0x1008, 0x100C], [0x100C, 0x100D]]
        self.assertEqual(
            coverage.runs_to_blocks(taken, blocks, insns, mapping),
            [0, 2, 3])
        fallthrough = [[0x1000, 0x1004], [0x1004, 0x1008],
                       [0x100C, 0x100D]]
        self.assertEqual(
            coverage.runs_to_blocks(fallthrough, blocks, insns, mapping),
            [0, 1, 3])

    def test_runs_starting_mid_block_are_mapped(self):
        insns, blocks, mapping, _ = fixture()
        ordered = coverage.runs_to_blocks(
            [[0x1001, 0x1004], [0x1008, 0x100C]],
            blocks, insns, mapping)
        self.assertEqual(ordered, [0, 2])

    def test_sorted_vectors_do_not_claim_edges(self):
        insns, blocks, mapping, successors = fixture()
        vector = {"executed": [0x1000, 0x1002, 0x1008]}
        covered_blocks, covered_edges = coverage.case_coverage(
            vector, blocks, insns, mapping, successors)
        self.assertEqual(covered_blocks, {0, 2})
        self.assertEqual(covered_edges, set())

    def test_ordered_transitions_only_edges(self):
        insns, blocks, mapping, successors = fixture()
        ordered = coverage.runs_to_blocks(
            [[0x1000, 0x1004], [0x1008, 0x100C], [0x100C, 0x100D]],
            blocks, insns, mapping)
        transitions = coverage.ordered_transitions(ordered)
        self.assertIn((0, 2), transitions)
        self.assertIn((2, 3), transitions)
        self.assertNotIn((0, 1), transitions)
        self.assertTrue(all(dst in successors[src]
                            for src, dst in transitions))

    def test_region_blocks_skips_data_ranges(self):
        insns, blocks, _mapping, successors = fixture()
        indices = coverage.region_blocks(
            blocks, insns, 0x1000, 0x1010, [(0x1008, 0x100A)], successors)
        self.assertEqual(indices, [0, 1, 3])

    def test_append_sites_filters_opcode_writes(self):
        accesses = [
            {"address": 0x1000, "target": "cmd_opcode", "access": "write"},
            {"address": 0x1002, "target": "cmd_arg1", "access": "write"},
            {"address": 0x1004, "target": "cmd_opcode", "access": "read"},
            {"address": 0x1006, "target": "cmd_opcode", "access": "read_write"},
        ]
        self.assertEqual(coverage.append_sites(accesses), [0x1000, 0x1006])

    def test_ichicheck_region_override(self):
        methods = [(0x46CB1C, "ichicheck"), (0x46F82E, "floor15"),
                   (0x4770E0, "dooropen")]
        regions = coverage.method_regions(methods)
        self.assertEqual(regions["ichicheck"], (0x46CB1C, 0x4770E0))


if __name__ == "__main__":
    unittest.main()
