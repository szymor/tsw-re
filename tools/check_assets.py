#!/usr/bin/env python3
"""Check the generated data/ tree against what the game expects.

Reports missing files, wrong picture sizes, a sprite mask that is not black
and white, and missing sounds or tunes; with --game-dir it also compares
every converted picture with its original pixel for pixel (the conversion
must be lossless: the 8-bit palettes are expanded, nothing else changes).
Exit status 1 when a required file is missing or a check fails.

Usage:
    tools/check_assets.py [--data data] [--game-dir "$GAME"]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # pragma: no cover - environment error
    sys.exit("Pillow is required: python3 -m pip install Pillow")

# name -> (size, required, source relative to the game dir)
PICTURES = {
    "Left": ((125, 400), True, "Data/Left.bmp"),
    "Right": ((125, 400), True, "Data/Right.bmp"),
    "LeftR": ((125, 400), False, "Data/LeftR.bmp"),
    "RightR": ((125, 400), False, "Data/RightR.bmp"),
    "CENTER": ((390, 400), True, "Data/CENTER.BMP"),
    "Chtable": ((352, 640), True, "Data/Chtable.oz"),
    "Chtableb": ((352, 480), False, "Data/Chtableb.oz"),
    "Key": ((32, 32), True, "Data/Key.oz"),
    "Player": ((384, 32), True, None),
    "open0": ((640, 400), False, "Data/open0.bmp"),
    "open1": ((640, 400), False, "Data/open1.bmp"),
    # Without their lettering (tools/make_clean_*.py): the game writes it.
    "title05_clean": ((390, 400), False, None),
    "title05R_clean": ((390, 400), False, None),
    "Image33_clean": ((640, 400), False, None),
    "Image34_clean": ((180, 36), False, None),
    "Icon": (None, False, None),
}
PICTURES.update({f"End{n:02d}": (None, False, f"Data2/End{n:02d}.bmp")
                 for n in range(5, 21)})
PICTURES.update({f"RankGlyph{i}": ((32, 32), False, None)
                 for i in range(1, 6)})

SOUNDS = ["open", "get", "taisen", "zeno", "3f", "20f", "kai", "kai2"]
TUNES = ["b_067xgw", "b_058xgw", "a_008xgw", "a_118xgw", "a_019xgw",
         "b_018xgw", "a_050xgw", "a_085xgw", "a_032xgw", "a_046xgw",
         "a_005xgw", "a_007xgw", "b_110xgw", "a_023xgw", "b_117xgw",
         "b_014xgw", "a_027xgw"]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--data", type=Path, default=Path("data"))
    ap.add_argument("--game-dir", type=Path,
                    help="compare the pictures with the originals")
    args = ap.parse_args()
    errors, warnings = [], []
    gfx = args.data / "gfx"

    for name, (size, required, source) in PICTURES.items():
        path = gfx / f"{name}.png"
        if not path.exists():
            (errors if required else warnings).append(f"missing {path}")
            continue
        with Image.open(path) as im:
            if size and im.size != size:
                errors.append(f"{path}: {im.size}, expected {size}")
            if args.game_dir and source and (args.game_dir / source).exists():
                with Image.open(args.game_dir / source) as orig:
                    a = im.convert("RGB").tobytes()
                    b = orig.convert("RGB").tobytes()
                    if a != b:
                        errors.append(f"{path}: pixels differ from {source}")

    mask = gfx / "Chtableb.png"
    if mask.exists():
        with Image.open(mask) as im:
            colours = {c for _, c in im.convert("RGB").getcolors(1 << 16)}
        stray = colours - {(0, 0, 0), (255, 255, 255)}
        if stray:
            # ImageList1 takes the mask as monochrome: these count as
            # sprite (assets.c), as in the original.
            warnings.append(f"{mask}: {len(stray)} grey shade(s) besides "
                            f"black and white (drawn as sprite)")

    audio = args.data / "audio"
    for name in SOUNDS:
        if not (audio / f"{name}.wav").exists():
            warnings.append(f"missing {audio}/{name}.wav (silent sound)")
    for name in TUNES:
        if not (audio / f"{name}.ogg").exists():
            warnings.append(f"missing {audio}/{name}.ogg (no music there)")

    initial = args.data / "maps" / "initial.bin"
    if not initial.exists():
        errors.append(f"missing {initial} (tools/extract_maps.py)")
    for name in ("messages.tsv", "ui.tsv"):
        path = args.data / "text" / "en" / name
        if not path.exists():
            warnings.append(f"missing {path} (tools/extract_messages.py)")

    for w in warnings:
        print(f"warning: {w}")
    for e in errors:
        print(f"error: {e}")
    print(f"{len(errors)} error(s), {len(warnings)} warning(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
