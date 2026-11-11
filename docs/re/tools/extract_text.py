#!/usr/bin/env python3
"""Extract text strings from the user's own TSW.exe.

The main form resource `TTSW10` contains a large Delphi string list.  This
script extracts the RCDATA resource and scans the DFM stream for
`0x06 <len> <ansi-string>` records (the DFM `vaString` encoding), writing
the strings in file order to a local text file.

Usage:
    extract_text.py /path/to/TSW.exe
    extract_text.py /path/to/TSW.exe -o /tmp/messages.raw.tsv

The default output is outside the repository:
    $XDG_CACHE_HOME/tsw/messages.raw.tsv  (or ~/.cache/tsw/messages.raw.tsv)

The generated file is copyrighted game text.  Keep it outside the repository;
do not commit it.
"""

import argparse
import os
import struct
import sys


def get_resource(exe_path, wanted_name):
    d = open(exe_path, "rb").read()
    if d[:2] != b"MZ":
        raise SystemExit("not a PE file")
    e = struct.unpack_from("<I", d, 0x3C)[0]
    if d[e:e+4] != b"PE\0\0":
        raise SystemExit("not a PE file")
    nsec = struct.unpack_from("<H", d, e + 6)[0]
    opt = e + 24
    sec = opt + struct.unpack_from("<H", d, e + 20)[0]
    sections = []
    for i in range(nsec):
        off = sec + i * 40
        name = d[off:off+8].rstrip(b"\0").decode("latin1")
        vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", d, off + 8)
        sections.append((name, vaddr, vsize, raddr, rsize))

    def rva2off(rva):
        for name, va, vs, ra, rs in sections:
            if va <= rva < va + max(vs, rs):
                return ra + (rva - va)
        return None

    rsrc = next((s for s in sections if s[0] == ".rsrc"), None)
    if not rsrc:
        raise SystemExit("no .rsrc section")
    base = rsrc[3]

    def parse_dir(off, path=()):
        _chars, _ts, _maj, _min, nname, nid = struct.unpack_from("<IIHHHH", d, off)
        out = []
        for i in range(nname + nid):
            eo = off + 16 + i * 8
            nameid, off2 = struct.unpack_from("<II", d, eo)
            if nameid & 0x80000000:
                so = base + (nameid & 0x7FFFFFFF)
                ln = struct.unpack_from("<H", d, so)[0]
                nm = d[so+2:so+2+ln*2].decode("utf-16le")
            else:
                nm = nameid
            if off2 & 0x80000000:
                out += parse_dir(base + (off2 & 0x7FFFFFFF), path + (nm,))
            else:
                de = base + off2
                drva, dsize, _cp, _res = struct.unpack_from("<IIII", d, de)
                out.append((path + (nm,), drva, dsize))
        return out

    for path, rva, size in parse_dir(base):
        if len(path) >= 2 and path[0] == 10 and path[1] == wanted_name:
            off = rva2off(rva)
            if off is None:
                continue
            return d[off:off+size]
    raise SystemExit(f"resource {wanted_name!r} not found")


def scan_dfm(data):
    results = []
    i = 0
    n = len(data)
    while i + 2 < n:
        if data[i] == 0x06:
            ln = data[i+1]
            if 1 <= ln <= 240 and i + 2 + ln <= n:
                s = data[i+2:i+2+ln]
                if all(32 <= c < 127 for c in s):
                    text = s.decode("latin1")
                    if len(text) >= 4:
                        results.append((i, text))
                    i += 2 + ln
                    continue
        i += 1
    return results


def default_output():
    cache = os.environ.get("XDG_CACHE_HOME")
    if not cache:
        cache = os.path.join(os.path.expanduser("~"), ".cache")
    return os.path.join(cache, "tsw", "messages.raw.tsv")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("-o", "--output", default=default_output())
    args = ap.parse_args()
    data = get_resource(args.exe, "TTSW10")
    rows = scan_dfm(data)
    outdir = os.path.dirname(args.output)
    if outdir:
        os.makedirs(outdir, exist_ok=True)
    with open(args.output, "w", encoding="utf-8") as f:
        f.write("# Raw extraction from the user's own TSW.exe. Do not redistribute.\n")
        f.write("# Refine into text/messages.tsv when the format is stable.\n")
        f.write("ordinal\toffset\ttext\n")
        for i, (off, text) in enumerate(rows):
            f.write(f"{i}\t{off:08x}\t{text}\n")
    print(f"wrote {args.output}: {len(rows)} strings")


if __name__ == "__main__":
    main()
