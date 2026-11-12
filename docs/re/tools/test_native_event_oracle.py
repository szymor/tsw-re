#!/usr/bin/env python3
"""Optional tests for the experimental native event harness.

These tests require the `unicorn` Python package.  They are skipped in the
normal system Python because the analysis venv is optional.  The pilot inputs
are synthetic snapshots so the tests do not depend on a mutable user save.
"""

from __future__ import annotations

import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

try:
    import unicorn  # noqa: F401
    HAVE_UNICORN = True
except ImportError:
    HAVE_UNICORN = False

import event_snapshot  # noqa: E402
import native_event_oracle as native  # noqa: E402
import run_native_oracle  # noqa: E402

EXPECTED_COMMANDS = {
    "floor10": (462, 19, {"event_selector": 5}),
    "roujin": (6, 50, {"message_selector": 25, "event_selector": 66}),
    "openning2": (651, 49, {"event_selector": 103}),
    "zenolivework": (36, 28, {}),
    "floor3_open": (41, 164, {"event_selector": 103}),
}


def synthetic_snapshot() -> event_snapshot.Snapshot:
    state = [0] * 171
    state[0] = 1000
    state[1] = 100
    state[2] = 100
    return event_snapshot.Snapshot(
        state, bytes(6273), aux=dict(event_snapshot.AUX_DEFAULTS),
        source="synthetic-pilot")


@unittest.skipUnless(HAVE_UNICORN, "unicorn is not installed")
class NativeHarnessTests(unittest.TestCase):
    def test_syokidata_self_test(self):
        exe = run_native_oracle.DEFAULT_EXE
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

    def test_pilot_native_traces(self):
        exe = run_native_oracle.DEFAULT_EXE
        if not exe.exists():
            self.skipTest("original executable not present")
        snapshot = synthetic_snapshot()
        for name, (expected_commands, expected_stubs, aux_expected) in \
                EXPECTED_COMMANDS.items():
            with self.subTest(name=name):
                trace = run_native_oracle.run_case(exe, snapshot, name)
                self.assertTrue(trace["ok"], trace["error"])
                self.assertEqual(trace["termination"], "returned")
                self.assertFalse(trace["tainted"])
                self.assertFalse(trace["command_truncated"])
                self.assertEqual(trace["stub_mode"], "presentation")
                self.assertEqual(trace["commands"], expected_commands)
                self.assertEqual(trace["stub_count"], expected_stubs)
                for aux_name, value in aux_expected.items():
                    self.assertEqual(trace["aux_final"][aux_name], value)
                repeat = run_native_oracle.run_case(exe, snapshot, name)
                self.assertEqual(trace, repeat)

        roujin = run_native_oracle.run_case(exe, snapshot, "roujin")
        self.assertEqual(
            roujin["command_words"][:6],
            [[0, 0, 0], [2, 0, 0], [0, 0, 0],
             [1, 2, 0], [0, 0, 0], [0, 0, 0]])

        floor3 = run_native_oracle.run_case(exe, snapshot, "floor3_open")
        self.assertEqual(floor3["command_words"][0], [10, 10, 0])
        self.assertEqual(floor3["state_diff"], [])
        self.assertEqual(
            floor3["map_diff"],
            [{"index": 2 + 3 * 123 + 92, "before": 0x07, "after": 0x06}])
        self.assertEqual(floor3["aux_final"]["event_selector"], 103)

    def _run_snippet(self, code: bytes, presentation: bool = False):
        exe = run_native_oracle.DEFAULT_EXE
        if not exe.exists():
            self.skipTest("original executable not present")
        harness = native.NativeHarness(
            exe, presentation_stubs=presentation)
        address = native.STACK_BASE + native.STACK_SIZE - 0x2000
        harness.uc.mem_write(address, code + b"\xC3")
        state = [0] * 171
        state[0] = 1000
        state[1] = 100
        state[2] = 100
        state[0x010 // 4] = 1
        return harness.call(address, state, bytes(6273), 1)

    def test_unknown_direct_call_fails_closed(self):
        call_address = native.STACK_BASE + native.STACK_SIZE - 0x2000
        target = 0x00500000
        rel = target - (call_address + 5)
        code = b"\xE8" + struct.pack("<i", rel)
        for presentation in (False, True):
            with self.subTest(presentation=presentation):
                result = self._run_snippet(code, presentation=presentation)
                self.assertFalse(result.ok)
                self.assertEqual(result.termination, "unsupported_transfer")
                self.assertIn("target=0x00500000", result.error)

    def test_unknown_indirect_call_fails_closed(self):
        exe = run_native_oracle.DEFAULT_EXE
        if not exe.exists():
            self.skipTest("original executable not present")
        for presentation in (False, True):
            with self.subTest(presentation=presentation):
                harness = native.NativeHarness(
                    exe, presentation_stubs=presentation)
                pointer = native.OBJECT_BASE + 0x800
                harness.uc.mem_write(pointer, (0).to_bytes(4, "little"))
                address = native.STACK_BASE + native.STACK_SIZE - 0x2000
                code = b"\xFF\x15" + pointer.to_bytes(4, "little") + b"\xC3"
                harness.uc.mem_write(address, code)
                state = [0] * 171
                state[0] = 1000
                state[1] = 100
                state[2] = 100
                state[0x010 // 4] = 1
                result = harness.call(address, state, bytes(6273), 1)
                self.assertFalse(result.ok)
                self.assertEqual(result.termination, "unsupported_transfer")
                self.assertIn("target=0x00000000", result.error)

    def test_unsupported_indirect_jump_fails_closed(self):
        exe = run_native_oracle.DEFAULT_EXE
        if not exe.exists():
            self.skipTest("original executable not present")
        harness = native.NativeHarness(exe, presentation_stubs=True)
        pointer = native.OBJECT_BASE + 0x800
        harness.uc.mem_write(pointer, (0x00500000).to_bytes(4, "little"))
        address = native.STACK_BASE + native.STACK_SIZE - 0x2000
        code = b"\xFF\x25" + pointer.to_bytes(4, "little") + b"\xC3"
        harness.uc.mem_write(address, code)
        state = [0] * 171
        state[0] = 1000
        state[1] = 100
        state[2] = 100
        state[0x010 // 4] = 1
        result = harness.call(address, state, bytes(6273), 1)
        self.assertFalse(result.ok)
        self.assertEqual(result.termination, "unsupported_transfer")
        self.assertIn("unsupported_indirect_jump", result.error)

    def test_truncated_capture_is_not_success(self):
        code = (b"\xC7\x05" + (native.COUNT_ADDR).to_bytes(4, "little")
                + (2000).to_bytes(4, "little"))
        result = self._run_snippet(code)
        self.assertFalse(result.ok)
        self.assertTrue(result.command_truncated)
        self.assertEqual(len(result.commands), 1024)

    def test_unclassified_stub_entry_fails_closed(self):
        pointer = native.OBJECT_BASE + 0x800
        exe = run_native_oracle.DEFAULT_EXE
        if not exe.exists():
            self.skipTest("original executable not present")
        harness = native.NativeHarness(exe, presentation_stubs=True)
        harness.uc.mem_write(pointer, (native.STUB_BASE + 1).to_bytes(4, "little"))
        address = native.STACK_BASE + native.STACK_SIZE - 0x2000
        code = b"\xFF\x15" + pointer.to_bytes(4, "little") + b"\xC3"
        harness.uc.mem_write(address, code)
        state = [0] * 171
        state[0] = 1000
        state[1] = 100
        state[2] = 100
        state[0x010 // 4] = 1
        result = harness.call(address, state, bytes(6273), 1)
        self.assertFalse(result.ok)
        self.assertEqual(result.termination, "unsupported_transfer")
        self.assertIn("0x0bad0001", result.error)

    def test_openning31_fails_closed(self):
        exe = run_native_oracle.DEFAULT_EXE
        if not exe.exists():
            self.skipTest("original executable not present")
        state = [0] * 171
        state[0] = 1000
        state[1] = 100
        state[2] = 100
        state[0x010 // 4] = 1
        state[0x018 // 4] = 5
        state[0x01C // 4] = 10
        aux = dict(event_snapshot.AUX_DEFAULTS)
        aux.update({"floor": 1, "x": 5, "y": 10, "sound_state": 0})
        harness = native.NativeHarness(exe, presentation_stubs=True)
        result = harness.call(0x4653B8, state, bytes(6273), 1, aux)
        self.assertFalse(result.ok)
        self.assertEqual(result.termination, "unsupported_transfer")
        self.assertIn("0x00403924", result.error)


if __name__ == "__main__":
    unittest.main()
