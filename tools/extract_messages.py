#!/usr/bin/env python3
"""Extract the game's dialogue list into data/text/messages.tsv.

The original shows every event dialogue from one Delphi string list: the
``Items`` of ``ListBox2``, reached by the event code through the form field
at +0x44c (``zyouout`` 0x449e18, see docs/re/18-messages.md).  The engine
loads the list at runtime and indexes it exactly like the original:

* sub-event id < 10 (``roujin``, NPCs):  index = message_selector + 0x3e
* op 0x0a sub-events (id >= 10):        index = arg1 - 1

It also writes ``ui.tsv`` beside it: default texts of form components the
engine shows outside dialogues (index 0: ``Edit8.Text``, the status bar text
shown during the launch intro).

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
# Default texts of form components the engine shows, written to ui.tsv in the
# same format, by index.  0: Edit8.Text, the status bar under the map; the
# form shows it until disp first writes the bar, i.e. during the launch
# intro (timer4ontimer).
UI_TEXTS = [("Edit8", "Text")]


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
    ui_path = args.output.with_name("ui.tsv")
    nodes = {n["name"]: n for n in read_dfm.walk(tree)}
    with ui_path.open("w", encoding="utf-8", newline="\n") as f:
        for i, (name, prop) in enumerate(UI_TEXTS):
            value = nodes.get(name, {}).get("props", {}).get(prop)
            if isinstance(value, str):
                f.write(f"{i}\t{escape(value)}\n")
    print(f"wrote {len(UI_TEXTS)} form texts to {ui_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
