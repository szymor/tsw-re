#!/usr/bin/env python3
"""Unit tests for the whole-binary event-buffer scanner."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import scan_event_buffers as scanner  # noqa: E402


class ScannerTests(unittest.TestCase):
    def scan(self, insns):
        methods = [(0x1000, "test_handler")]
        return scanner.scan(insns, methods, methods)

    def test_direct_count_and_indexed_append(self):
        insns = [
            (0x1000, "mov ebx,0x48c5ac"),
            (0x1005, "mov esi,0x48c74c"),
            (0x100A, "inc DWORD PTR [ebx]"),
            (0x100C, "mov eax,DWORD PTR [ebx]"),
            (0x100E, "lea eax,[eax+eax*2]"),
            (0x1011, "mov WORD PTR [esi+eax*2],0xd"),
            (0x1016, "mov WORD PTR [esi+eax*2+0x2],0x76"),
            (0x101B, "mov WORD PTR [esi+eax*2+0x4],0x6"),
        ]
        accesses, base_loads, unresolved = self.scan(insns)
        targets = {(a["target"], a["access"]) for a in accesses}
        self.assertIn(("count", "read_write"), targets)
        self.assertIn(("count", "read"), targets)
        self.assertIn(("cmd_opcode", "write"), targets)
        self.assertIn(("cmd_arg1", "write"), targets)
        self.assertIn(("cmd_arg2", "write"), targets)
        self.assertEqual(len(base_loads), 2)
        self.assertEqual(unresolved, [])

    def test_interior_block_keeps_live_in_registers(self):
        methods = [(0x1000, "dispatcher"), (0x1010, "handler")]
        insns = [
            (0x1000, "mov ebx,0x48c5ac"),
            (0x1005, "mov esi,0x48c74c"),
            (0x100A, "jmp 0x1010"),
            (0x1010, "inc DWORD PTR [ebx]"),
            (0x1012, "mov WORD PTR [esi],0x1"),
        ]
        accesses, _, _ = scanner.scan(insns, methods, [(0x1000, "dispatcher")])
        self.assertTrue(any(a["target"] == "count" for a in accesses))
        self.assertTrue(any(a["target"] == "cmd_opcode" for a in accesses))

    def test_handler_seed_supplies_live_ins(self):
        methods = [(0x1000, "dispatcher"), (0x1010, "handler")]
        insns = [
            (0x1000, "ret"),
            (0x1010, "inc DWORD PTR [ebx]"),
            (0x1012, "mov WORD PTR [esi],0x1"),
        ]
        accesses, _, _ = scanner.scan(
            insns, methods, [(0x1000, "dispatcher")], {0x1010}
        )
        self.assertTrue(any(a["target"] == "count" for a in accesses))
        self.assertTrue(any(a["target"] == "cmd_opcode" for a in accesses))

    def test_lea_alias_propagates(self):
        insns = [
            (0x1000, "mov esi,0x48c74c"),
            (0x1005, "lea edi,[esi+0x2]"),
            (0x1009, "mov WORD PTR [edi],0x1"),
        ]
        accesses, _, _ = self.scan(insns)
        self.assertTrue(any(a["target"] == "cmd_arg1" for a in accesses))

    def test_stack_shadow_does_not_restore_wrong_slot(self):
        insns = [
            (0x1000, "mov esi,0x48c74c"),
            (0x1005, "push esi"),
            (0x1006, "push 0x0"),
            (0x100B, "pop edi"),
            (0x100C, "mov WORD PTR [edi],0x1"),
        ]
        accesses, _, _ = self.scan(insns)
        self.assertFalse(any(a["target"] == "cmd_opcode" for a in accesses))

    def test_stack_shadow_crosses_block_boundaries(self):
        insns = [
            (0x1000, "mov esi,0x48c74c"),
            (0x1005, "push esi"),
            (0x1006, "jmp 0x1010"),
            (0x1010, "pop edi"),
            (0x1011, "mov WORD PTR [edi],0x1"),
        ]
        accesses, _, _ = self.scan(insns)
        self.assertTrue(any(a["target"] == "cmd_opcode" for a in accesses))

    def test_memory_pop_consumes_shadow_slot(self):
        insns = [
            (0x1000, "push 0x0"),
            (0x1005, "mov esi,0x48c74c"),
            (0x100A, "push esi"),
            (0x100B, "pop DWORD PTR [ebx]"),
            (0x1011, "pop edi"),
            (0x1012, "mov WORD PTR [edi],0x1"),
        ]
        accesses, _, _ = self.scan(insns)
        self.assertFalse(any(a["target"] == "cmd_opcode" for a in accesses))

    def test_pop_through_esp_invalidates_shadow_stack(self):
        insns = [
            (0x1000, "mov esi,0x48c74c"),
            (0x1005, "push esi"),
            (0x1006, "push 0x0"),
            (0x100B, "pop DWORD PTR [esp]"),
            (0x1011, "pop edi"),
            (0x1012, "mov WORD PTR [edi],0x1"),
        ]
        accesses, _, _ = self.scan(insns)
        self.assertFalse(any(a["target"] == "cmd_opcode" for a in accesses))

    def test_absolute_destination_is_a_write(self):
        insns = [
            (0x1000, "mov WORD PTR ds:0x48c74c,0x1"),
            (0x1006, "inc DWORD PTR ds:0x48c5ac"),
            (0x100C, "mov ds:0x48c5ac,eax"),
        ]
        accesses, _, _ = self.scan(insns)
        targets = {(a["target"], a["access"]) for a in accesses}
        self.assertIn(("cmd_opcode", "write"), targets)
        self.assertIn(("count", "read_write"), targets)
        self.assertIn(("count", "write"), targets)

    def test_conflicting_bases_report_both_targets(self):
        insns = [
            (0x1000, "mov ebx,0x48c5ac"),
            (0x1005, "mov esi,0x48c74c"),
            (0x100A, "cmp eax,0"),
            (0x100D, "je 0x1020"),
            (0x100F, "mov esi,0x48c5ac"),
            (0x1014, "jmp 0x1030"),
            (0x1020, "mov esi,0x48c74c"),
            (0x1025, "jmp 0x1030"),
            (0x1030, "mov WORD PTR [esi],0x1"),
        ]
        accesses, _, _ = self.scan(insns)
        targets = {(a["target"], a["access"]) for a in accesses}
        self.assertIn(("count", "write"), targets)
        self.assertIn(("cmd_opcode", "write"), targets)

    def test_indexed_lea_is_reported_not_dropped(self):
        insns = [
            (0x1000, "mov esi,0x48c74c"),
            (0x1005, "lea edi,[esi+eax*2]"),
            (0x1009, "mov WORD PTR [edi],0x1"),
        ]
        accesses, _, unresolved = self.scan(insns)
        self.assertFalse(any(a["target"] == "cmd_opcode" for a in accesses))
        self.assertTrue(any("unknown offset" in u["reason"] for u in unresolved))

    def test_pointer_can_leave_and_reenter_buffer(self):
        insns = [
            (0x1000, "mov esi,0x48c74c"),
            (0x1005, "add esi,0x1000"),
            (0x100A, "sub esi,0x1000"),
            (0x100F, "mov WORD PTR [esi],0x1"),
        ]
        accesses, _, _ = self.scan(insns)
        self.assertTrue(any(a["target"] == "cmd_opcode" for a in accesses))

    def test_fallback_accesses_are_marked(self):
        methods = [(0x1000, "handler")]
        insns = [
            (0x1000, "mov ebx,0x48c5ac"),
            (0x1005, "mov esi,0x48c74c"),
            (0x100A, "ret"),
            (0x1010, "je 0x1020"),
            (0x1016, "jmp 0x1030"),
            (0x1020, "inc DWORD PTR [ebx]"),
            (0x1022, "cmp eax,0"),
            (0x1025, "jne 0x1030"),
            (0x1027, "mov WORD PTR [esi],0x1"),
            (0x102C, "jmp 0x1030"),
            (0x1030, "nop"),
        ]
        accesses, _, _ = self.scan(insns)
        # 0x1020 is seeded directly; 0x1027 is an unseeded fall-through
        # successor and must inherit fallback provenance.
        at_1027 = [a for a in accesses if a["address"] == 0x1027]
        self.assertTrue(at_1027)
        self.assertTrue(all("fallback" in str(a["mechanism"]) for a in at_1027))
        self.assertTrue(all(a["confidence"] == "medium" for a in at_1027))

    def test_register_valued_pointer_adjustment_is_not_dropped(self):
        insns = [
            (0x1000, "mov esi,0x48c74c"),
            (0x1005, "add esi,eax"),
            (0x1008, "mov WORD PTR [esi],0x1"),
        ]
        accesses, _, unresolved = self.scan(insns)
        self.assertFalse(any(a["target"] == "cmd_opcode" for a in accesses))
        self.assertTrue(any("unknown offset" in u["reason"] for u in unresolved))

    def test_pointer_adjusting_loop_terminates(self):
        insns = [
            (0x1000, "mov esi,0x48c74c"),
            (0x1005, "add esi,0x2"),
            (0x1008, "cmp eax,0"),
            (0x100B, "jne 0x1005"),
            (0x100D, "mov WORD PTR [esi],0x1"),
        ]
        accesses, _, unresolved = self.scan(insns)
        # The loop must widen to unknown-offset provenance and stop growing.
        self.assertTrue(unresolved)
        self.assertTrue(any(
            "unknown offset" in u["reason"] or "possible base+disp" in u["reason"]
            for u in unresolved
        ))

    def test_unknown_offset_is_reported(self):
        insns = [
            (0x1000, "mov esi,0x48c74c"),
            (0x1005, "mov WORD PTR [esi+0x20],0x1"),
        ]
        _, _, unresolved = self.scan(insns)
        self.assertEqual(len(unresolved), 1)
        self.assertIn("not a known field", unresolved[0]["reason"])

    def test_inventory_rows_have_seven_columns(self):
        accesses = [{
            "address": 0x1234, "method": "test", "access": "write",
            "target": "cmd_opcode", "mechanism": "direct",
            "confidence": "high", "instruction": "mov WORD PTR [esi],0x1",
        }]
        base_loads = [{
            "address": 0x1230, "method": "test",
            "instruction": "mov esi,0x48c74c", "register": "esi",
            "base": scanner.CMD_BASE,
        }]
        unresolved = [{
            "address": 0x1240, "method": "test",
            "instruction": "mov WORD PTR [esi+0x20],0x1",
            "reason": "base+disp 0x48c76c is not a known field",
        }]
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "writers.tsv"
            scanner.write_inventory(path, "test.exe", "0" * 64,
                                    accesses, base_loads, unresolved)
            for line in path.read_text(encoding="utf-8").splitlines():
                if not line or line.startswith("#"):
                    continue
                self.assertEqual(len(line.split("\t")), 7, line)


if __name__ == "__main__":
    unittest.main()
