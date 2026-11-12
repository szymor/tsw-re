#!/usr/bin/env python3
"""Read the TTSW10 form (DFM) and its published-field table from TSW.exe.

The game's text lives in string-list properties of components on the main
form (for example the dialogue list the event code reads through the form
field at +0x44c).  This tool joins two sources from the user's own binary:

* the binary DFM resource ``TTSW10`` (``TPF0`` stream): component tree,
  classes, names and properties, including ``Strings`` lists;
* the ``TTSW10`` published-field table (VMT field table; Delphi 2 layout): form field
  offset -> component name, which is how the code addresses components
  (``mov eax, [form + 0x44c]``).

So a runtime access ``form+OFF -> Items/Lines.Strings[i]`` maps to a named
component and an ordinal: the namespace-aware message key ``(list, index)``
the event analysis needs (11-event-analysis-plan.md WS5).

Output is generated data containing copyrighted game text; it is written
outside the repository (default ``$XDG_CACHE_HOME/tsw/``).

Usage:
    read_dfm.py TSW.exe --fields                 # offset -> name table
    read_dfm.py TSW.exe --lists                  # components with string lists
    read_dfm.py TSW.exe --list-at 0x44c          # strings of the list at +0x44c
    read_dfm.py TSW.exe --json                   # full dump to the cache dir
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import extract_text  # noqa: E402  (PE resource reader)

IMAGE_BASE = 0x400000
FORM_CLASS = "TTSW10"


def default_out_dir() -> Path:
    base = os.environ.get("XDG_CACHE_HOME") or str(Path.home() / ".cache")
    return Path(base) / "tsw"


# --------------------------------------------------------------------------
# PE helpers


class PE:
    def __init__(self, data: bytes):
        self.data = data
        e = struct.unpack_from("<I", data, 0x3C)[0]
        nsec = struct.unpack_from("<H", data, e + 6)[0]
        opt = e + 24
        sec = opt + struct.unpack_from("<H", data, e + 20)[0]
        self.base = struct.unpack_from("<I", data, opt + 28)[0]
        self.sections = []
        for i in range(nsec):
            off = sec + i * 40
            vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, off + 8)
            self.sections.append((vaddr, max(vsize, rsize), raddr, rsize))

    def off(self, va: int) -> int | None:
        rva = va - self.base
        for vaddr, size, raddr, rsize in self.sections:
            if vaddr <= rva < vaddr + size and rva - vaddr < rsize:
                return raddr + (rva - vaddr)
        return None

    def u32(self, va: int) -> int:
        o = self.off(va)
        if o is None:
            raise ValueError(f"unmapped VA 0x{va:08x}")
        return struct.unpack_from("<I", self.data, o)[0]

    def u16(self, va: int) -> int:
        return struct.unpack_from("<H", self.data, self.off(va))[0]

    def shortstr(self, va: int) -> str:
        o = self.off(va)
        n = self.data[o]
        return self.data[o + 1:o + 1 + n].decode("latin1")


def find_class_slot(pe: PE, class_name: str) -> int:
    """Address of the VMT's class-name slot for class_name.

    TSW.exe uses the Delphi 2 VMT layout (no vmtSelfPtr/vmtIntfTable): the
    class-name slot is at VMT - 28, and the tables are addressed relative to
    it: type info -16, field table -12, method table -8, class name 0,
    instance size +4, parent +8.  A candidate slot is accepted when its type-info pointer
    names the same class (tkClass record: kind byte, then the name).
    """
    needle = bytes([len(class_name)]) + class_name.encode("latin1")
    data = pe.data
    names = []
    i = -1
    while True:
        i = data.find(needle, i + 1)
        if i < 0:
            break
        for vaddr, size, raddr, rsize in pe.sections:
            if raddr <= i < raddr + rsize:
                names.append(pe.base + vaddr + (i - raddr))
    for name_va in names:
        ptr = struct.pack("<I", name_va)
        j = -1
        while True:
            j = data.find(ptr, j + 1)
            if j < 0:
                break
            for vaddr, size, raddr, rsize in pe.sections:
                if not raddr <= j < raddr + rsize:
                    continue
                slot = pe.base + vaddr + (j - raddr)
                try:
                    ti = pe.u32(slot - 16)
                    if pe.shortstr(ti + 1) == class_name:
                        return slot
                except (ValueError, TypeError, IndexError):
                    pass
    raise ValueError(f"VMT of {class_name} not found")


def class_name_at(pe: PE, vmt_slot: int) -> str:
    return pe.shortstr(pe.u32(vmt_slot))


def field_table(pe: PE, slot: int) -> list[dict]:
    """Published fields of the class whose class-name slot is `slot`."""
    ft = pe.u32(slot - 12)
    if not ft:
        return []
    count = pe.u16(ft)
    class_tab = pe.u32(ft + 2)
    classes = []
    if class_tab:
        n = pe.u16(class_tab)
        for k in range(n):
            ref = pe.u32(class_tab + 2 + 4 * k)    # class (VMT) pointer
            try:
                # Delphi 2: the class-name slot is at VMT - 28.
                name = pe.shortstr(pe.u32(ref - 28))
            except (ValueError, TypeError, IndexError):
                name = "?"
            classes.append(name)
    out = []
    p = ft + 6
    for _ in range(count):
        offset = pe.u32(p)
        index = pe.u16(p + 4)
        name = pe.shortstr(p + 6)
        out.append({"offset": offset, "name": name,
                    "class": classes[index] if index < len(classes) else "?"})
        p += 6 + 1 + len(name)
    return out


# --------------------------------------------------------------------------
# DFM (TPF0) parser


class DFM:
    def __init__(self, data: bytes):
        if data[:4] != b"TPF0":
            raise ValueError("not a binary DFM (TPF0)")
        self.d = data
        self.p = 4

    def byte(self) -> int:
        v = self.d[self.p]
        self.p += 1
        return v

    def shortstr(self) -> str:
        n = self.byte()
        s = self.d[self.p:self.p + n].decode("latin1")
        self.p += n
        return s

    def int_(self, fmt: str) -> int:
        v = struct.unpack_from(fmt, self.d, self.p)[0]
        self.p += struct.calcsize(fmt)
        return v

    def value(self):
        t = self.byte()
        if t == 0:                      # vaNull
            return None
        if t == 1:                      # vaList
            items = []
            while self.d[self.p] != 0:
                items.append(self.value())
            self.p += 1
            return items
        if t == 2:
            return self.int_("<b")
        if t == 3:
            return self.int_("<h")
        if t == 4:
            return self.int_("<i")
        if t == 5:                      # vaExtended
            self.p += 10
            return "<extended>"
        if t in (6, 7):                 # vaString, vaIdent
            return self.shortstr()
        if t == 8:
            return False
        if t == 9:
            return True
        if t == 10:                     # vaBinary
            n = self.int_("<I")
            self.p += n
            return f"<binary {n}>"
        if t == 11:                     # vaSet
            items = []
            while True:
                s = self.shortstr()
                if not s:
                    break
                items.append(s)
            return items
        if t == 12:                     # vaLString
            n = self.int_("<I")
            s = self.d[self.p:self.p + n].decode("latin1")
            self.p += n
            return s
        if t == 13:                     # vaNil
            return None
        if t == 14:                     # vaCollection
            items = []
            while self.d[self.p] != 0:
                if self.d[self.p] in (2, 3, 4):
                    self.value()        # order index
                if self.byte() != 1:    # vaList marker
                    raise ValueError("bad collection item")
                items.append(self.props())
            self.p += 1
            return items
        if t == 15:
            self.p += 4
            return "<single>"
        if t in (16, 17):
            self.p += 8
            return "<8 bytes>"
        if t == 18:                     # vaWString
            n = self.int_("<I")
            s = self.d[self.p:self.p + 2 * n].decode("utf-16-le")
            self.p += 2 * n
            return s
        if t == 19:
            return self.int_("<q")
        if t == 20:                     # vaUTF8String
            n = self.int_("<I")
            s = self.d[self.p:self.p + n].decode("utf-8")
            self.p += n
            return s
        raise ValueError(f"unknown DFM value type {t} at {self.p - 1}")

    def props(self) -> dict:
        out = {}
        while True:
            name = self.shortstr()
            if not name:
                return out
            out[name] = self.value()

    def component(self) -> dict:
        if self.d[self.p] & 0xF0 == 0xF0:   # filer flags
            flags = self.byte()
            if flags & 2:                    # ffChildPos
                self.value()
        cls = self.shortstr()
        name = self.shortstr()
        props = self.props()
        children = []
        while self.d[self.p] != 0:
            children.append(self.component())
        self.p += 1
        return {"class": cls, "name": name, "props": props,
                "children": children}


def walk(node: dict):
    yield node
    for c in node["children"]:
        yield from walk(c)


def string_lists(node: dict) -> dict[str, list[str]]:
    """Properties that hold string lists (Items.Strings, Lines.Strings...)."""
    return {k: v for k, v in node["props"].items()
            if k.endswith(".Strings") and isinstance(v, list)}


def load(exe: Path) -> tuple[dict, list[dict]]:
    data = exe.read_bytes()
    raw = extract_text.get_resource(str(exe), FORM_CLASS)
    tree = DFM(raw).component()
    pe = PE(data)
    fields = field_table(pe, find_class_slot(pe, FORM_CLASS))
    return tree, fields


def list_at(tree: dict, fields: list[dict], offset: int) -> tuple[dict, dict]:
    field = next((f for f in fields if f["offset"] == offset), None)
    if field is None:
        raise SystemExit(f"no published field at +0x{offset:x}")
    node = next((n for n in walk(tree) if n["name"] == field["name"]), None)
    if node is None:
        raise SystemExit(f"field {field['name']} has no DFM component")
    return field, node


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("exe", type=Path)
    ap.add_argument("--fields", action="store_true")
    ap.add_argument("--lists", action="store_true")
    ap.add_argument("--list-at", type=lambda s: int(s, 0))
    ap.add_argument("--json", action="store_true")
    ap.add_argument("-o", "--output", type=Path)
    args = ap.parse_args()
    tree, fields = load(args.exe)
    if args.fields:
        for f in fields:
            print(f"+0x{f['offset']:03x}  {f['class']:16} {f['name']}")
    if args.lists:
        by_name = {f["name"]: f["offset"] for f in fields}
        for n in walk(tree):
            for prop, items in string_lists(n).items():
                off = by_name.get(n["name"])
                where = f"+0x{off:03x}" if off is not None else "      "
                print(f"{where}  {n['class']:14} {n['name']}.{prop}: "
                      f"{len(items)} strings")
    if args.list_at is not None:
        field, node = list_at(tree, fields, args.list_at)
        lists = string_lists(node)
        for prop, items in lists.items():
            print(f"# +0x{args.list_at:x} {node['class']} {node['name']}.{prop}")
            for i, s in enumerate(items):
                print(f"{i}\t0x{i:x}\t{s}")
    if args.json:
        out = args.output or default_out_dir() / "ttsw10_dfm.json"
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps({"fields": fields, "tree": tree},
                                  indent=1) + "\n")
        print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
