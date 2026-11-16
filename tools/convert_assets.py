#!/usr/bin/env python3
"""Convert the original 640x400 Data/ assets to build-local PNG files.

Generated files go to data/gfx/ (ignored by Git).  The `.oz` atlases are
8-bit BMP containers despite the extension, so Pillow decodes them directly;
`--tile-atlas` remains an escape hatch for a pre-extracted image.  The original
TSW.exe icon is also extracted into the generated Icon.png.

Audio goes to data/audio/ (also ignored): the Wav/ sounds are copied with
lower-case names, and the Midi/ tunes are rendered to Ogg Vorbis (FluidSynth
with a General MIDI SoundFont, then ffmpeg), so the game needs no MIDI
synthesizer.  `--soundfont` picks the SoundFont, `--no-audio` skips audio.
"""

from __future__ import annotations

import argparse
import io
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # pragma: no cover - environment error
    sys.exit("Pillow is required: python3 -m pip install Pillow")


BMP_NAMES = [
    "Left.bmp",
    "Right.bmp",
    "LeftR.bmp",
    "RightR.bmp",
    "CENTER.BMP",
    "title05.bmp",
    "title05R.bmp",
    "open0.bmp",
    "open1.bmp",
    "open2.bmp",
]

# The floor-50 ending's pictures (openning9 phases 4..6, Data2/).
END_NAMES = [f"End{n:02d}.bmp" for n in range(5, 21)] + ["End26.bmp"]

# Pictures embedded in the TTSW10 form (Picture.Data): Image21 the ending's
# closing text (phase 5), Image33 "The End" (phase 7), Image34 the "back
# side" button on Panel2 (phase 8).
DFM_PICTURES = ["Image21", "Image33", "Image34"]

OZ_NAMES = [
    "Chtable.oz",
    "Chtableb.oz",
    "Key.oz",
]


def extract_dfm_pictures(exe_path: Path, out_dir: Path) -> None:
    root = Path(__file__).resolve().parent.parent
    sys.path.insert(0, str(root / "docs" / "re" / "tools"))
    import read_dfm  # noqa: E402

    tree, _ = read_dfm.load(exe_path, keep_binary=True)
    nodes = {n["name"]: n for n in read_dfm.walk(tree)}
    for name in DFM_PICTURES:
        bmp = read_dfm.picture_bitmap(nodes[name]) if name in nodes else None
        if not bmp:
            print(f"skip missing form picture {name}")
            continue
        dst = out_dir / f"{name}.png"
        with Image.open(io.BytesIO(bmp)) as im:
            im.convert("RGBA").save(dst, "PNG")
        print(f"extracted form picture {name} -> {dst}")


def extract_rank_glyphs(exe_path: Path, out_dir: Path) -> None:
    """Form2's BitBtn1..5 glyphs (TBitBtn.Glyph.Data: a u32 size, then the
    bitmap file) as RankGlyph1..5.png.  TBitBtn draws a glyph with its
    bottom-left pixel's colour transparent."""
    root = Path(__file__).resolve().parent.parent
    sys.path.insert(0, str(root / "docs" / "re" / "tools"))
    import extract_text  # noqa: E402
    import read_dfm  # noqa: E402

    raw = extract_text.get_resource(str(exe_path), "TFORM2")
    tree = read_dfm.DFM(raw, True).component()
    nodes = {n["name"]: n for n in read_dfm.walk(tree)}
    for i in range(1, 6):
        blob = nodes.get(f"BitBtn{i}", {}).get("props", {}).get("Glyph.Data")
        if not isinstance(blob, (bytes, bytearray)) or len(blob) < 4:
            print(f"skip missing ranking glyph {i}")
            continue
        size = int.from_bytes(blob[:4], "little")
        with Image.open(io.BytesIO(bytes(blob[4:4 + size]))) as im:
            rgba = im.convert("RGBA")
        key = rgba.getpixel((0, rgba.height - 1))
        px = rgba.load()
        for y in range(rgba.height):
            for x in range(rgba.width):
                if px[x, y] == key:
                    px[x, y] = (0, 0, 0, 0)
        dst = out_dir / f"RankGlyph{i}.png"
        rgba.save(dst, "PNG")
        print(f"extracted ranking glyph {i} -> {dst}")


SOUNDFONT_DIRS = [Path("/usr/share/soundfonts"), Path("/usr/share/sounds/sf2"),
                  Path("/usr/share/soundfonts/default")]


def find_soundfont(explicit: str | None) -> Path | None:
    """The SoundFont for the MIDI tunes: --soundfont, $TSW_SOUNDFONT, else
    one from the system folders, preferring HiDef, then the usual defaults,
    then the smallest."""
    for c in (explicit, os.environ.get("TSW_SOUNDFONT")):
        if c:
            p = Path(c)
            if not p.exists():
                sys.exit(f"SoundFont not found: {p}")
            return p
    found = sorted({p for d in SOUNDFONT_DIRS if d.is_dir()
                    for p in d.glob("*.sf[23]")})
    for key in ("hidef", "default", "fluidr3", "generaluser"):
        for p in found:
            if key in p.name.lower():
                return p
    return min(found, key=lambda p: p.stat().st_size) if found else None


def convert_audio(game_dir: Path, out_dir: Path, soundfont: str | None) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    for src in sorted((game_dir / "Wav").glob("*")):
        if src.suffix.lower() == ".wav":
            dst = out_dir / src.name.lower()
            shutil.copyfile(src, dst)
            print(f"copied {src.name} -> {dst}")
    mids = sorted(p for p in (game_dir / "Midi").glob("*")
                  if p.suffix.lower() == ".mid")
    if not mids:
        print("skip missing Midi/")
        return
    sf = find_soundfont(soundfont)
    fluidsynth, ffmpeg = shutil.which("fluidsynth"), shutil.which("ffmpeg")
    if not sf or not fluidsynth or not ffmpeg:
        print("skip the MIDI tunes: needs fluidsynth, ffmpeg and a SoundFont"
              " (--soundfont)")
        return
    print(f"rendering the MIDI tunes with {sf}")
    with tempfile.TemporaryDirectory() as tmp:
        for src in mids:
            wav = Path(tmp) / (src.stem.lower() + ".wav")
            dst = out_dir / (src.stem.lower() + ".ogg")
            subprocess.run([fluidsynth, "-ni", "-q", "-r", "44100", "-g", "0.6",
                            "-F", str(wav), str(sf), str(src)],
                           check=True, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
            # Trim the silence after the last note so the tune loops as the
            # original's MediaPlayer5 restart did; keep the start as is.
            subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-i", str(wav),
                            "-af", "areverse,silenceremove=start_periods=1:"
                            "start_threshold=-70dB,areverse",
                            "-c:a", "libvorbis", "-q:a", "5", str(dst)],
                           check=True)
            print(f"rendered {src.name} -> {dst}")


def convert_image(src: Path, dst: Path) -> None:
    with Image.open(src) as im:
        im.convert("RGBA").save(dst, "PNG")
    print(f"converted {src.name} -> {dst}")


def make_player_sheet(data_dir: Path, out_dir: Path) -> None:
    """Extract the masked player frames from Chtable.oz.

    Per facing (down, left, right, up: sprites 184/192/200/208) three poses,
    each a color sprite plus the adjacent black-on-white mask: +0 and +2 the
    walking poses yusyahyouji1 draws into the two map buffers after a step
    (0xfa/0xfc - 0x42), +4 the standing pose Timer1Timer redraws (0xfe -
    0x42).  Frame (facing - 1) * 3 + pose, 32 px each.
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
    for base in (184, 192, 200, 208):
        for pose in (0, 2, 4):
            color = sprite(base + pose)
            mask = sprite(base + pose + 1).convert("L")
            alpha = mask.point(lambda p: 0 if p > 127 else 255)
            color.putalpha(alpha)
            frames.append(color)

    sheet = Image.new("RGBA", (32 * len(frames), 32), (0, 0, 0, 0))
    for i, frame in enumerate(frames):
        sheet.paste(frame, (i * 32, 0), frame)
    dst = out_dir / "Player.png"
    sheet.save(dst, "PNG")
    print(f"generated masked player sheet -> {dst}")




def extract_pe_icon(exe_path: Path, out_dir: Path) -> None:
    """Extract the original Windows icon from TSW.exe into Icon.png.

    The icon is generated build data; it must not be committed.  The PE
    resource tree holds one RT_GROUP_ICON and its RT_ICON image.
    """
    if not exe_path.exists():
        print(f"skip missing {exe_path}")
        return
    d = exe_path.read_bytes()
    if d[:2] != b"MZ":
        print(f"skip non-PE icon source {exe_path}")
        return
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    if d[pe:pe+4] != b"PE\0\0":
        print(f"skip malformed PE {exe_path}")
        return
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", d, opt)[0]
    dd = opt + (96 if magic == 0x10B else 112)
    res_rva, _res_size = struct.unpack_from("<II", d, dd + 2 * 8)
    sec = opt + struct.unpack_from("<H", d, pe + 20)[0]
    sections = []
    for i in range(nsec):
        off = sec + i * 40
        name = d[off:off+8].rstrip(b"\0").decode("latin1")
        vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", d, off + 8)
        sections.append((name, vaddr, vsize, raddr, rsize))

    def rva2off(rva):
        for _name, va, vs, ra, rs in sections:
            if va <= rva < va + max(vs, rs):
                return ra + (rva - va)
        return None

    base = rva2off(res_rva)
    if base is None:
        print("skip icon: resource section not mapped")
        return

    def dir_name(value):
        if value & 0x80000000:
            so = base + (value & 0x7FFFFFFF)
            ln = struct.unpack_from("<H", d, so)[0]
            return d[so+2:so+2+ln*2].decode("utf-16le", "replace")
        return str(value)

    resources = []

    def walk(off, path=()):
        _chars, _ts, _maj, _min, nname, nid = struct.unpack_from("<IIHHHH", d, off)
        for i in range(nname + nid):
            eo = off + 16 + i * 8
            nameid, off2 = struct.unpack_from("<II", d, eo)
            nm = dir_name(nameid)
            if off2 & 0x80000000:
                walk(base + (off2 & 0x7FFFFFFF), path + (nm,))
            else:
                de = base + off2
                drva, dsize, _cp, _res = struct.unpack_from("<IIII", d, de)
                off = rva2off(drva)
                if off is not None:
                    resources.append((path + (nm,), off, dsize))

    walk(base)
    groups = [r for r in resources if r[0][0] == "14"]
    icons = [r for r in resources if r[0][0] == "3"]
    if not groups or not icons:
        print("skip icon: no RT_GROUP_ICON/RT_ICON resource")
        return

    _path, goff, gsize = groups[0]
    group = d[goff:goff+gsize]
    if len(group) < 6:
        print("skip icon: malformed group")
        return
    _reserved, _typ, count = struct.unpack_from("<HHH", group, 0)
    if count == 0 or len(group) < 6 + count * 14:
        print("skip icon: empty group")
        return

    best = None
    for i in range(count):
        width, height, colors, reserved, planes, bits, size, icon_id = struct.unpack_from(
            "<BBBBHHIH", group, 6 + i * 14)
        area = (width or 256) * (height or 256)
        if best is None or area > best[0]:
            best = (area, width, height, colors, reserved, planes, bits, size,
                    icon_id)
    _area, width, height, colors, reserved, planes, bits, size, icon_id = best
    icon_id = str(icon_id)
    icon = next((r for r in icons if r[0][1] == icon_id), None)
    if icon is None:
        print(f"skip icon: RT_ICON {icon_id} not found")
        return
    _path, ioff, isize = icon
    image = d[ioff:ioff+isize]
    if size and len(image) > size:
        image = image[:size]

    header = struct.pack("<HHH", 0, 1, 1)
    entry = struct.pack("<BBBBHHII", width, height, colors, reserved, planes,
                        bits, len(image), 22)
    ico = header + entry + image
    try:
        from PIL import Image
        with Image.open(io.BytesIO(ico)) as im:
            im.convert("RGBA").save(out_dir / "Icon.png", "PNG")
        print(f"extracted original icon -> {out_dir / 'Icon.png'}")
    except Exception as exc:
        print(f"skip icon extraction: {exc}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", required=True,
                        help="Tower of the Sorcerer installation directory")
    parser.add_argument("--out", default="data/gfx",
                        help="output directory (default: data/gfx)")
    parser.add_argument("--audio-out", default="data/audio",
                        help="audio output directory (default: data/audio)")
    parser.add_argument("--soundfont", default=None,
                        help="SoundFont for rendering the MIDI tunes")
    parser.add_argument("--no-audio", action="store_true",
                        help="skip the sounds and tunes")
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

    data2_dir = game_dir / "Data2"
    for name in END_NAMES:
        src = data2_dir / name
        if not src.exists():
            print(f"skip missing Data2/{name}")
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
    extract_pe_icon(game_dir / "TSW.exe", out_dir)
    extract_dfm_pictures(game_dir / "TSW.exe", out_dir)
    extract_rank_glyphs(game_dir / "TSW.exe", out_dir)
    if not args.no_audio:
        convert_audio(game_dir, Path(args.audio_out), args.soundfont)

    print("done")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
