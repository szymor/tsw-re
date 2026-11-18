#!/usr/bin/env python3
"""Make data/gfx/Image33_clean.png and Image34_clean.png: the ending's
pictures without their English lettering, for the game to write the
text over in the player's language (src/pictures.c).

    Image33  "The End", the original's credits and "See you again!",
             white lettering on the night sky and the hills' speckles
    Image34  the "See you again!" button, cut from the same art

The two verse pictures need no cleaning: without its lettering open2
is open1 (the same ring, a shade two to four levels darker) and
Image21 is End26 (plain white), so the game writes the verses over
those.

The lettering is every pixel within two pixels of a bright grey one
(the letters' cores and their bevel), and each is refilled from the
nearest pixel that is not lettering, in a random direction (fixed
seed), so the plain sky stays plain and the hills' speckles go on
behind where the words were.  None of the lettering's bright pixels may
be left afterwards.

The pictures themselves stay as converted from the original game
(tools/check_assets.py compares them).  Needs Pillow and NumPy.

Usage: tools/make_clean_pictures.py [--data DIR]
"""

import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def load(gfx, name):
    return np.asarray(Image.open(gfx / f"{name}.png").convert("RGB")).astype(
        np.int32)


def save(gfx, name, rgb):
    path = gfx / f"{name}_clean.png"
    Image.fromarray(rgb.astype(np.uint8), "RGB").save(path, optimize=True)
    print(f"wrote {path}")


def grow(mask, r):
    out = mask.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            out |= np.roll(np.roll(mask, dy, 0), dx, 1)
    return out


def refill(rgb, letters, rng):
    """Each lettering pixel takes the nearest non-lettering pixel's colour
    along a random direction."""
    h, w = letters.shape
    ys, xs = np.nonzero(letters)
    for y, x in zip(ys, xs):
        for _ in range(64):
            a = rng.uniform(0, 2 * np.pi)
            dy, dx = np.sin(a), np.cos(a)
            for step in range(1, 40):
                sy, sx = int(round(y + dy * step)), int(round(x + dx * step))
                if not (0 <= sy < h and 0 <= sx < w):
                    break
                if not letters[sy, sx]:
                    rgb[y, x] = rgb[sy, sx]
                    break
            else:
                continue
            break
        else:
            sys.exit(f"make_clean_pictures: no backdrop near ({x}, {y})")


def night(gfx, name, boxes):
    rgb = load(gfx, name)
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    bright = (r == g) & (r >= 150) & (b - r <= 60)
    inside = np.zeros_like(bright)
    for y0, y1, x0, x1 in boxes:
        inside[y0:y1, x0:x1] = True
    letters = grow(bright & inside, 2) & inside
    refill(rgb, letters, np.random.default_rng(1))
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    left = (r == g) & (r >= 150) & (b - r <= 60) & inside
    if left.any():
        sys.exit(f"make_clean_pictures: {name}: {int(left.sum())} "
                 "lettering pixels left")
    save(gfx, name, rgb)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--data", type=Path,
                    default=Path(__file__).resolve().parent.parent / "data")
    args = ap.parse_args()
    gfx = args.data / "gfx"
    # "The End", the credits, "See you again!" (rows, columns).
    night(gfx, "Image33", [(55, 125, 335, 630), (140, 190, 370, 610),
                           (368, 396, 455, 635)])
    night(gfx, "Image34", [(0, 36, 0, 180)])


if __name__ == "__main__":
    main()
