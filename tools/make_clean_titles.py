#!/usr/bin/env python3
"""Make data/gfx/title05_clean.png and title05R_clean.png: the title
pictures without their credit lettering, for the game to write the
credits over (src/render.c, draw_center_picture).

title05.bmp and title05R.bmp end with two lines of black, shadowed
lettering,

    (C) 1998 - 2000 Oz & Kenichi
    (C) 1996               N.W

on a stone texture of three colours.  The texture repeats, but not as
one tile: along a row it repeats every 32 pixels except in a strip on
the right, and down a column every 66 rows (title05) or 32 (title05R).
So every 8x8 block of the band is refilled from wherever the texture
around it matches best: of a few thousand shifts (any along the row,
or up and down the column), the one that agrees with the most plain
texture pixels in a 24x24 window around the block, among those whose
source pixels are all plain texture outside the band.  The lettering,
its shadow and its edge pixels are all the band pixels that are not one
of the three colours, and none may be left afterwards.

The lettered originals are read from --source, where
tools/convert_assets.py puts them (--lettered-out); they are not in
data/, since the game only draws the clean copies.  Needs Pillow and
NumPy.

Usage: tools/make_clean_titles.py [--data DIR] [--source DIR]
"""

import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image

LEFT, RIGHT = 7, 378     # inside the picture's bevel
BAND_ROWS = (290, 361)   # rows searched for lettering and its shadow
CLEAN_ROWS = [*range(283, 290), *range(361, 392)]  # plain texture only
LAST_ROW = 392           # sources come from rows LAST_SOURCE_TOP..391
LAST_SOURCE_TOP = 281    # below the monster strip
BLOCK = 8
WINDOW = 8               # the score window reaches this far past a block
MIN_SCORE = 0.95


def shifts():
    out = [(dx, 0) for dx in range(-(RIGHT - LEFT), RIGHT - LEFT) if dx]
    for dy in range(-(LAST_ROW - LAST_SOURCE_TOP), LAST_ROW - LAST_SOURCE_TOP):
        if dy:
            out += [(dx, dy) for dx in (-64, -32, 0, 32, 64)]
    return out


def clean(src, dst):
    rgb = np.asarray(Image.open(src).convert("RGB")).astype(np.int64)
    key = (rgb[..., 0] << 16) | (rgb[..., 1] << 8) | rgb[..., 2]
    palette = np.unique(key[CLEAN_ROWS, LEFT:RIGHT])
    if len(palette) > 4:
        sys.exit(f"make_clean_titles: {src.name}: the rows around the "
                 f"credits are not plain texture ({len(palette)} colours)")
    plain = np.isin(key, palette)
    y0, y1 = BAND_ROWS
    lettering = np.zeros_like(plain)
    lettering[y0:y1, LEFT:RIGHT] = ~plain[y0:y1, LEFT:RIGHT]
    source_ok = plain & ~lettering
    source_ok[:LAST_SOURCE_TOP] = False
    source_ok[LAST_ROW:] = False
    source_ok[:, :LEFT] = False
    source_ok[:, RIGHT:] = False

    out = key.copy()
    worst = 1.0
    candidates = shifts()
    for by in range(y0, y1, BLOCK):
        for bx in range(LEFT, RIGHT, BLOCK):
            ty, tx = np.nonzero(lettering[by:by + BLOCK, bx:bx + BLOCK])
            if not len(ty):
                continue
            ty += by
            tx += bx
            wy0, wx0 = max(by - WINDOW, 0), max(bx - WINDOW, LEFT)
            wy1 = by + BLOCK + WINDOW
            wx1 = min(bx + BLOCK + WINDOW, RIGHT)
            ky, kx = np.nonzero(source_ok[wy0:wy1, wx0:wx1] &
                                ~lettering[wy0:wy1, wx0:wx1])
            ky += wy0
            kx += wx0
            best = None
            for dx, dy in candidates:
                sy, sx = ty + dy, tx + dx
                if (sy.min() < 0 or sy.max() >= key.shape[0] or
                        sx.min() < 0 or sx.max() >= key.shape[1] or
                        not source_ok[sy, sx].all()):
                    continue
                qy, qx = ky + dy, kx + dx
                inside = ((qy >= 0) & (qy < key.shape[0]) &
                          (qx >= 0) & (qx < key.shape[1]))
                qy, qx = qy[inside], qx[inside]
                usable = source_ok[qy, qx]
                n = int(usable.sum())
                if n < 40:
                    continue
                score = float((key[qy, qx][usable] ==
                               key[ky[inside], kx[inside]][usable]).mean())
                rank = (score, -(abs(dx) + abs(dy)))
                if best is None or rank > best[0]:
                    best = (rank, dx, dy)
            if best is None or best[0][0] < MIN_SCORE:
                got = "none" if best is None else f"{best[0][0]:.3f}"
                sys.exit(f"make_clean_titles: {src.name}: no texture "
                         f"matches the block at ({bx}, {by}) (best {got})")
            worst = min(worst, best[0][0])
            out[ty, tx] = key[ty + best[2], tx + best[1]]

    left = ~np.isin(out[y0:y1, LEFT:RIGHT], palette)
    if left.any():
        sys.exit(f"make_clean_titles: {src.name}: {int(left.sum())} "
                 "lettering pixels left")
    pixels = np.stack([(out >> 16) & 255, (out >> 8) & 255, out & 255],
                      axis=-1).astype(np.uint8)
    Image.fromarray(pixels, "RGB").save(dst, optimize=True)
    print(f"wrote {dst} (worst block match {worst:.3f})")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--data", type=Path,
                    default=Path(__file__).resolve().parent.parent / "data")
    ap.add_argument("--source", type=Path,
                    default=Path.home() / ".cache" / "tsw" / "lettered",
                    help="the lettered originals (convert_assets.py "
                         "--lettered-out)")
    args = ap.parse_args()
    gfx = args.data / "gfx"
    src = args.source
    for name in ("title05", "title05R"):
        clean(src / f"{name}.png", gfx / f"{name}_clean.png")


if __name__ == "__main__":
    main()
