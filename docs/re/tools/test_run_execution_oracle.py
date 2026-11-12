#!/usr/bin/env python3
"""Optional tests for the P3b execution oracle (run_execution_oracle.py).

Require the `unicorn` package and the user's own TSW.exe (default Wine path,
or $TSW_EXE).  Skipped otherwise.  The floor-3 case runs ~700 native
stackwork steps and takes tens of seconds.
"""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

try:
    import unicorn  # noqa: F401
    HAVE_UNICORN = True
except ImportError:
    HAVE_UNICORN = False

import export_event_vectors as vectors  # noqa: E402
import run_execution_oracle as oracle  # noqa: E402

EXE = Path(os.environ.get("TSW_EXE", str(vectors.DEFAULT_EXE)))
WITNESS = {w.case_id: w for w in vectors.WITNESSES}


def state_after(report: dict) -> dict[int, int]:
    return {d["index"]: d["after"] for d in report["drain"]["state_diff"]}


@unittest.skipUnless(HAVE_UNICORN, "unicorn is not installed")
class ExecutionOracleTests(unittest.TestCase):
    def setUp(self):
        if not EXE.exists():
            self.skipTest("original TSW.exe not present")

    def test_roujin_drains_without_drain_writes(self):
        r = oracle.run_case(EXE, WITNESS["roujin"], 100, permissive_vcl=True,
                            complete_dialogs=True)
        self.assertTrue(r["builder"]["ok"])
        self.assertEqual(r["drain"]["stop"], "drained")
        self.assertEqual(r["drain"]["steps"], 6)
        self.assertFalse(any(s["writes"] for s in r["drain"]["trace"]))
        # roujin writes the event selector as a state index (0x449cc0).
        builder = {(w["index"], w["eip"]) for w in r["builder"]["writes"]}
        self.assertIn((0x42, 0x449CC0), builder)
        self.assertIn((0x62, 0x449805), builder)

    def test_strict_drain_fails_closed_on_media(self):
        r = oracle.run_case(EXE, WITNESS["zenolivework"], 100)
        self.assertTrue(r["drain"]["stop"].startswith("step_failed"))
        self.assertFalse(r["drain"]["tainted"])

    def test_floor3_opening_end_to_end(self):
        r = oracle.run_case(EXE, WITNESS["handan_floor3"], 4000,
                            permissive_vcl=True, complete_dialogs=True)
        d = r["drain"]
        self.assertEqual(d["stop"], "modal_idle")
        self.assertTrue(d["tainted"])
        self.assertEqual([m["subevent"] for m in d["modal"]],
                         [20, 21, 11, 11, 12, 13])
        self.assertEqual(d["modal"][0]["count_after"], 651)   # openning2
        st = state_after(r)
        self.assertEqual(st[0], 400)
        self.assertEqual(st[1], 10)
        self.assertEqual(st[2], 10)
        self.assertEqual(st[4], 2)       # floor
        self.assertEqual(st[5], 2)       # floor limit
        self.assertEqual((st[6], st[7]), (2, 7))
        for flag in (0x5B, 0x5C, 0x5D, 0x64, 0x65):
            self.assertEqual(st[flag], 1, hex(flag))
        self.assertNotIn(0x0C, st)       # set by openning31, cleared by 13
        self.assertEqual([(m["index"], m["after"]) for m in d["map_diff"]],
                         [(2 + 3 * 123 + 92, 0x06)])


if __name__ == "__main__":
    unittest.main()
