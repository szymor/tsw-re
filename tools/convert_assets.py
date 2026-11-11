#!/usr/bin/env python3
"""Convert the original 640x400 Data/ assets to build-local PNG files.

Generated files go to data/gfx/ (ignored by Git).  The `.oz` atlases are
8-bit BMP containers despite the extension, so Pillow decodes them directly;
`--tile-atlas` remains an escape hatch for a pre-extracted image.
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # pragma: no cover - environment error
    sys.exit("Pillow is required: python3 -m pip install Pillow")


BMP_NAMES = [
    "Left.bmp",
    "Right.bmp",
    "CENTER.BMP",
    "title05.bmp",
    "open0.bmp",
    "open1.bmp",
    "open2.bmp",
]

OZ_NAMES = [
    "Chtable.oz",
    "Chtableb.oz",
    "Key.oz",
]


def convert_image(src: Path, dst: Path) -> None:
    with Image.open(src) as im:
        im.convert("RGBA").save(dst, "PNG")
    print(f"converted {src.name} -> {dst}")


def make_player_sheet(data_dir: Path, out_dir: Path) -> None:
    """Extract the four masked player facing frames from Chtable.oz.

    The original player frames are color sprite 184/192/200/208 plus the
    adjacent black-on-white transparency mask.  Frame order matches the
    original facing values: down, left, right, up.
    """
    atlas_path = data_dir / "Chtable.oz"
    if not atlas_path.exists():
        sys.exit(f"player sprite source missing: {atlas_path}")
    with Image.open(atlas_path) as src:
        atlas = src.convert("RGBA")

    def sprite(index: int) -> Image.Image:
        col = index % 11
        row = index // 11
        return atlas.crop((col * 32, row * 32, (col + 1) * 32, (row + 1) * 32))

    frames = []
    for color_index in (184, 192, 200, 208):
        color = sprite(color_index)
        mask = sprite(color_index + 1).convert("L")
        alpha = mask.point(lambda p: 0 if p > 127 else 255)
        color.putalpha(alpha)
        frames.append(color)

    sheet = Image.new("RGBA", (32 * 4, 32), (0, 0, 0, 0))
    for i, frame in enumerate(frames):
        sheet.paste(frame, (i * 32, 0), frame)
    dst = out_dir / "Player.png"
    sheet.save(dst, "PNG")
    print(f"generated masked player sheet -> {dst}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", required=True,
                        help="Tower of the Sorcerer installation directory")
    parser.add_argument("--out", default="data/gfx",
                        help="output directory (default: data/gfx)")
    parser.add_argument("--tile-atlas", default=None,
                        help="optional override: copy this PNG to Chtable.png")
    args = parser.parse_args()

    game_dir = Path(args.game_dir)
    data_dir = game_dir / "Data"
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    if not data_dir.is_dir():
        sys.exit(f"missing original Data directory: {data_dir}")

    for name in BMP_NAMES:
        src = data_dir / name
        if not src.exists():
            print(f"skip missing {name}")
            continue
        convert_image(src, out_dir / (src.stem + ".png"))

    for name in OZ_NAMES:
        src = data_dir / name
        if not src.exists():
            print(f"skip missing {name}")
            continue
        convert_image(src, out_dir / (src.stem + ".png"))

    # Optional override for experiments with a pre-extracted atlas.  By
    # default Chtable.oz above is decoded directly into Chtable.png.
    if args.tile_atlas:
        atlas = Path(args.tile_atlas)
        if not atlas.exists():
            sys.exit(f"tile atlas not found: {atlas}")
        dst = out_dir / "Chtable.png"
        if atlas.resolve() != dst.resolve():
            shutil.copyfile(atlas, dst)
        print(f"overrode tile atlas with {atlas} -> {dst}")

    if not (out_dir / "Chtable.png").exists():
        sys.exit("Chtable atlas missing: expected Data/Chtable.oz")

    make_player_sheet(data_dir, out_dir)

    print("done")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
