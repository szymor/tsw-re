#!/usr/bin/env python3
"""Extract the original's dialogue list from TSW.exe, for comparison.

The original shows every event dialogue from one Delphi string list: the
``Items`` of ``ListBox2``, reached by the event code through the form field
at +0x44c (``zyouout`` 0x449e18, see docs/re/18-messages.md).  The engine
loads the list at runtime and indexes it exactly like the original:

* sub-event id < 10 (``roujin``, NPCs):  index = message_selector + 0x3e
* op 0x0a sub-events (id >= 10):        index = arg1 - 1

It also writes ``ui.tsv`` beside it: default texts of form components the
engine shows outside dialogues (index 0: ``Edit8.Text``, the status bar text
shown during the launch intro), then code literals (index 1: ``zyouout``'s
"try again?" ``MessageDlg``).

The game's text is committed: data/text/<lang>/messages.tsv and ui.tsv are
the source to edit and translate, and this extraction made the English one.
To keep edits safe, the output goes to the cache by default
($XDG_CACHE_HOME/tsw/text/, else ~/.cache/tsw/text/), where it can be
compared with the committed text; ``-o`` writes elsewhere.

Usage:
    python3 tools/extract_messages.py --game-dir "$GAME"
    diff ~/.cache/tsw/text/messages.tsv data/text/en/messages.tsv

Format: one line per entry, ``<index>\\t<text>``, index decimal from 0,
text with backslash, tab and newline escaped as ``\\\\``, ``\\t``, ``\\n``.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "docs" / "re" / "tools"))

import read_dfm  # noqa: E402

LIST_OFFSET = 0x44C        # TTSW10.ListBox2
# Default texts of form components the engine shows, written to ui.tsv in the
# same format, by index.  0: Edit8.Text, the status bar under the map; the
# form shows it until disp first writes the bar, i.e. during the launch
# intro (timer4ontimer).
UI_TEXTS = [("Edit8", "Text")]
# Literals in the code, appended to ui.tsv after UI_TEXTS: (virtual address,
# what shows it).  1: zyouout's (0x449e18) "try again?" MessageDlg after a
# lost event battle; 2: handan's status-line tail for a gem worth 1.
EXE_STRINGS = [(0x44A454, "zyouout MessageDlg"),
               (0x449008, "handan: a gem worth 1 point"),
               (0x47BDCC, "yousei: ranking sums broken"),
               (0x47BE08, "yousei: ranking version broken"),
               (0x47BE50, "yousei: ranking file unreadable"),
               (0x4375C8, "Form2: name prompt caption"),
               (0x4375A0, "Form2: name prompt text")]


def read_cstring(exe: Path, va: int) -> str | None:
    """The NUL-terminated ANSI string at a virtual address of the PE."""
    data = exe.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    sec = pe + 24 + opt_size
    rva = va - base
    for i in range(nsec):
        vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, sec + 40 * i + 8)
        if vaddr <= rva < vaddr + max(vsize, rsize):
            off = raddr + rva - vaddr
            end = data.index(b"\0", off)
            return data[off:end].decode("cp1252")
    return None


def escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--game-dir", type=Path)
    ap.add_argument("--exe", type=Path)
    cache = Path(os.environ.get("XDG_CACHE_HOME") or Path.home() / ".cache")
    ap.add_argument("-o", "--output", type=Path,
                    default=cache / "tsw" / "text" / "messages.tsv")
    args = ap.parse_args()
    exe = args.exe or (args.game_dir / "TSW.exe" if args.game_dir else None)
    if exe is None or not exe.exists():
        ap.error("give --game-dir or --exe pointing at the original TSW.exe")
    tree, fields = read_dfm.load(exe)
    field, node = read_dfm.list_at(tree, fields, LIST_OFFSET)
    lists = read_dfm.string_lists(node)
    items = lists.get("Items.Strings")
    if items is None:
        raise SystemExit(f"{field['name']} has no Items list")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="\n") as f:
        for i, s in enumerate(items):
            f.write(f"{i}\t{escape(s)}\n")
    print(f"wrote {len(items)} messages from {field['name']} to {args.output}")
    ui_path = args.output.with_name("ui.tsv")
    nodes = {n["name"]: n for n in read_dfm.walk(tree)}
    with ui_path.open("w", encoding="utf-8", newline="\n") as f:
        for i, (name, prop) in enumerate(UI_TEXTS):
            value = nodes.get(name, {}).get("props", {}).get(prop)
            if isinstance(value, str):
                f.write(f"{i}\t{escape(value)}\n")
        for j, (va, _what) in enumerate(EXE_STRINGS):
            value = read_cstring(exe, va)
            if value is not None:
                f.write(f"{len(UI_TEXTS) + j}\t{escape(value)}\n")
    print(f"wrote {len(UI_TEXTS) + len(EXE_STRINGS)} UI texts to {ui_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
