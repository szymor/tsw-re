#!/usr/bin/env python3
"""Extract the game's dialogue list into data/text/messages.tsv.

The original shows every event dialogue from one Delphi string list: the
``Items`` of ``ListBox2``, reached by the event code through the form field
at +0x44c (``zyouout`` 0x449e18, see docs/re/18-messages.md).  The engine
loads the list at runtime and indexes it exactly like the original:

* sub-event id < 10 (``roujin``, NPCs):  index = message_selector + 0x3e
* op 0x0a sub-events (id >= 10):        index = arg1 - 1

Like the converted art, the output is the user's own game data: it goes to
``data/`` (ignored by Git) and is installed with the build.  Nothing here is
committed.

Usage:
    python3 tools/extract_messages.py --game-dir "$GAME"
    python3 tools/extract_messages.py --exe "$GAME/TSW.exe" -o data/text/messages.tsv

Format: one line per entry, ``<index>\\t<text>``, index decimal from 0,
text with backslash, tab and newline escaped as ``\\\\``, ``\\t``, ``\\n``.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "docs" / "re" / "tools"))

import read_dfm  # noqa: E402

LIST_OFFSET = 0x44C        # TTSW10.ListBox2


def escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--game-dir", type=Path)
    ap.add_argument("--exe", type=Path)
    ap.add_argument("-o", "--output", type=Path,
                    default=ROOT / "data" / "text" / "messages.tsv")
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
    return 0


if __name__ == "__main__":
    sys.exit(main())
