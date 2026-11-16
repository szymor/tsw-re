#!/usr/bin/env python3
"""Checks for the layout oracle against bounds read by hand from the
decompiled handlers (19-windows.md).  Needs Unicorn and the user's TSW.exe
(TSW_EXE, default the Wine install); skipped otherwise."""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import form_layout  # noqa: E402

EXE = Path(os.environ.get("TSW_EXE", Path.home() / ".wine/drive_c/Program "
                          "Files (x86)/Tower of the Sorcerer/TSW.exe"))


@unittest.skipUnless(form_layout.Uc is not None and EXE.exists(),
                     "needs unicorn and TSW.exe")
class LayoutTests(unittest.TestCase):
    def model(self, form="TTSW10"):
        methods = form_layout.method_table(Path(__file__).resolve().parent.parent)
        m = form_layout.FormModel(EXE, 0, form)
        entry = form_layout.FORMS[form][1]
        m.run(methods[entry] if isinstance(entry, str) else entry)
        return m, methods

    def test_zyouout_places_button1_in_the_rich_edit(self):
        m, methods = self.model()
        m.run(methods["zyouout"])
        b = m.bounds()
        self.assertEqual(b["RichEdit1"], (129, 45, 382, 200))
        self.assertEqual(b["Button1"], (420, 204, 75, 25))

    def test_img6work_rows(self):
        m, methods = self.model()
        m.uc.mem_write(form_layout.G_STATE + 4 * 0x11, (1).to_bytes(4, "little"))
        m.run(methods["img6work"])
        b = m.bounds()
        self.assertEqual(b["RichEdit1"], (141, 16, 358, 56))
        self.assertEqual(b["Button36"], (435, 368, 64, 20))
        self.assertEqual(b["Image24"][:2], (16, 84))     # Panel1-relative

    def test_form2(self):
        m, _ = self.model("TForm2")
        b = m.bounds()
        self.assertEqual(b["Memo61"], (16, 8, 442, 28))
        self.assertEqual(b["BitBtn3"], (209, 44, 56, 56))
        self.assertEqual(b["Memo10"][:2], (16, 351))


if __name__ == "__main__":
    unittest.main()
