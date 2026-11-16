#!/usr/bin/env python3
"""Run TSW.exe's layout code on a model of the main form and print the
controls' final bounds.

The form's DFM gives each control's design-time bounds, but `syokidata0`
(and window openers such as `zyouout` or `img6work`) move most of them at
run time, depending on the layout option (`g_option_dat[8]`: 0 = 640x400,
1 = 800x500).  This tool executes those handlers in Unicorn on a form
whose published fields point at one object per control (bounds at
+0x24/+0x28/+0x2c/+0x30 from the DFM).  It performs the four bounds
setters (`TControl.SetLeft/SetTop/SetWidth/SetHeight`), answers the
form's client width/height, and skips every other call (pictures, fonts,
captions).  Skipped calls leave their stack arguments behind, so a run
ends when the handler's frame is gone; the bounds written up to then are
the result.

Usage:
    form_layout.py TSW.exe                      # after syokidata0
    form_layout.py TSW.exe --then zyouout img6work
    form_layout.py TSW.exe --json out.json
    form_layout.py TSW.exe --form TForm2        # the ranking form
"""

from __future__ import annotations

import argparse
import csv
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import read_dfm  # noqa: E402

try:
    from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EDX,
                                   UC_X86_REG_ESP, UC_X86_REG_EBP,
                                   UC_X86_REG_EIP)
except ImportError:                                    # pragma: no cover
    Uc = None

STACK_BASE, STACK_SIZE = 0x20000000, 0x100000
FORM_BASE = 0x10000000
OBJECTS = 0x11000000            # one 0x400-byte object per control
OBJECT_SIZE = 0x400
DUMMY = 0x12000000              # fonts, pictures, strings lists, ...
VTABLE = 0x12100000
RET_STUB = 0x12200000
SENTINEL = 0x12300000
FS_BASE = 0x30000000
G_TSW10 = 0x48C510
G_OPTION_DAT = 0x489B9C          # g_option_dat (byte [8] = layout)
G_STATE = 0x4B8688               # the state block (int32 words)

SETTERS = {0x413098: 0x24, 0x4130B8: 0x28, 0x4130D8: 0x2C, 0x4130F8: 0x30}
CLIENT_WIDTH, CLIENT_HEIGHT = 0x413178, 0x4131BC
SET_CLIENT_HEIGHT = 0x42151C     # TControl.SetClientHeight (syokidata0: 424)


def method_table(root: Path) -> dict[str, int]:
    rows = list(csv.reader(open(root / "method-table.tsv"), delimiter="\t"))
    h = rows[0]
    return {r[h.index("name")]: int(r[h.index("address")], 16)
            for r in rows[1:]}


FORMS = {
    # class -> (global form pointer, entry handler run first)
    "TTSW10": (G_TSW10, "syokidata0"),
    "TForm2": (0x48A764, 0x434BC4),     # the ranking form's formactivate
}


class FormModel:
    def __init__(self, exe: Path, layout: int = 0, form_class: str = "TTSW10"):
        if Uc is None:
            raise RuntimeError("unicorn is not installed")
        self.data, self.base, self.sections = self._pe(exe.read_bytes())
        raw = read_dfm.extract_text.get_resource(str(exe), form_class.upper())
        tree = read_dfm.DFM(raw, False).component()
        pe = read_dfm.PE(self.data)
        fields = read_dfm.field_table(pe, read_dfm.find_class_slot(pe,
                                                                   form_class))
        self.global_ptr = FORMS[form_class][0]
        self.nodes = {n["name"]: n for n in read_dfm.walk(tree)}
        self.fields = {f["offset"]: f["name"] for f in fields}
        self.form_props = tree["props"]
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self._map(layout)
        self.client_h = 0
        self.uc.hook_add(UC_HOOK_CODE, self._code)
        self.done = False

    @staticmethod
    def _pe(data: bytes):
        lfa = struct.unpack_from("<I", data, 0x3C)[0]
        n = struct.unpack_from("<H", data, lfa + 6)[0]
        opt = struct.unpack_from("<H", data, lfa + 20)[0]
        base = struct.unpack_from("<I", data, lfa + 24 + 0x1C)[0]
        secs = []
        for i in range(n):
            o = lfa + 24 + opt + 40 * i
            vs, va, rs, rp = struct.unpack_from("<IIII", data, o + 8)
            secs.append((va, max(vs, rs), rs, rp))
        return data, base, secs

    def _map(self, layout: int) -> None:
        uc = self.uc
        for va, size, rs, rp in self.sections:
            a = (self.base + va) & ~0xFFF
            end = (self.base + va + size + 0xFFF) & ~0xFFF
            uc.mem_map(a, end - a, 7)
            uc.mem_write(self.base + va, self.data[rp:rp + rs])
        uc.mem_map(0, 0x1000, 7)
        uc.mem_write(0, b"\xff\xff\xff\xff")
        for a, s in ((STACK_BASE, STACK_SIZE), (FORM_BASE, 0x1000),
                     (OBJECTS, 0x100000), (DUMMY, 0x1000), (VTABLE, 0x1000),
                     (RET_STUB, 0x1000), (SENTINEL, 0x1000),
                     (FS_BASE, 0x1000)):
            uc.mem_map(a, s, 7)
        uc.mem_write(FS_BASE, b"\xff\xff\xff\xff")
        uc.mem_write(RET_STUB, b"\xc3")
        uc.mem_write(VTABLE, struct.pack("<I", RET_STUB) * 0x400)
        dummy = struct.pack("<I", DUMMY) * (0x1000 // 4)
        uc.mem_write(DUMMY, dummy)
        uc.mem_write(DUMMY, struct.pack("<I", VTABLE))
        # The form: every slot points at the dummy, published fields at
        # their own control objects.
        uc.mem_write(FORM_BASE, struct.pack("<I", DUMMY) * 0x400)
        uc.mem_write(FORM_BASE, struct.pack("<I", VTABLE))
        p = self.form_props
        fw = p.get("ClientWidth", 640) + 6
        fh = p.get("ClientHeight", 426) + 25
        self._bounds(FORM_BASE, p.get("Left", 0), p.get("Top", 0), fw, fh)
        self.objects: dict[str, int] = {}
        for i, (off, name) in enumerate(sorted(self.fields.items())):
            obj = OBJECTS + i * OBJECT_SIZE
            self.objects[name] = obj
            uc.mem_write(obj, dummy[:OBJECT_SIZE])
            uc.mem_write(obj, struct.pack("<I", VTABLE))
            props = self.nodes.get(name, {}).get("props", {})
            self._bounds(obj, props.get("Left", 0), props.get("Top", 0),
                         props.get("Width", 0), props.get("Height", 0))
            uc.mem_write(FORM_BASE + off, struct.pack("<I", obj))
        uc.mem_write(self.global_ptr, struct.pack("<I", FORM_BASE))
        uc.mem_write(G_OPTION_DAT + 8, bytes([layout]))

    def _bounds(self, obj: int, l: int, t: int, w: int, h: int) -> None:
        self.uc.mem_write(obj + 0x24, struct.pack("<iiii", l, t, w, h))

    def _code(self, uc, address, size, _user):
        if address == SENTINEL:
            self.done = True
            uc.emu_stop()
            return
        code = bytes(uc.mem_read(address, size))
        target = None
        if code[0] == 0xE8:
            target = address + 5 + struct.unpack_from("<i", code, 1)[0]
        elif code[0] == 0xFF and size >= 2 and (code[1] >> 3) & 7 == 2:
            target = -1                         # indirect call: skip
        else:
            return
        eax = uc.reg_read(UC_X86_REG_EAX)
        edx = uc.reg_read(UC_X86_REG_EDX)
        result = DUMMY
        if target in SETTERS and OBJECTS <= eax < OBJECTS + 0x100000 or \
                target in SETTERS and eax == FORM_BASE:
            uc.mem_write(eax + SETTERS[target],
                         struct.pack("<i", struct.unpack("<i", struct.pack(
                             "<I", edx))[0]))
        elif target == SET_CLIENT_HEIGHT and eax == FORM_BASE:
            self.client_h = struct.unpack("<i", struct.pack("<I", edx))[0]
        elif target in (CLIENT_WIDTH, CLIENT_HEIGHT) and eax == FORM_BASE:
            # A bsSingle form: 3 px frame each side, 19 px caption; a
            # SetClientHeight wins over the outer height.
            w, h = struct.unpack("<ii", bytes(uc.mem_read(eax + 0x2C, 8)))
            if target == CLIENT_WIDTH:
                result = w - 6
            else:
                result = self.client_h if self.client_h else h - 25
        uc.reg_write(UC_X86_REG_EAX, result)
        uc.reg_write(UC_X86_REG_EIP, address + size)

    def run(self, entry: int, steps: int = 200000) -> None:
        uc = self.uc
        sp = STACK_BASE + STACK_SIZE - 0x1000
        uc.mem_write(sp, struct.pack("<I", SENTINEL))
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_EBP, sp + 0x100)
        uc.reg_write(UC_X86_REG_EAX, FORM_BASE)
        self.done = False
        try:
            uc.emu_start(entry, SENTINEL + 1, count=steps)
        except UcError:
            pass                    # the skipped calls' stack drift

    def bounds(self) -> dict[str, tuple[int, int, int, int]]:
        out = {}
        for name, obj in self.objects.items():
            out[name] = struct.unpack("<iiii",
                                      bytes(self.uc.mem_read(obj + 0x24, 16)))
        return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("exe", type=Path)
    ap.add_argument("--layout", type=int, default=0)
    ap.add_argument("--form", default="TTSW10", choices=sorted(FORMS),
                    help="TForm2: the ranking form after its formactivate")
    ap.add_argument("--then", nargs="*", default=[],
                    help="handlers run after syokidata0 (method names)")
    ap.add_argument("--only", nargs="*", help="control names to print")
    ap.add_argument("--state", nargs="*", default=[],
                    help="state words before the handlers: INDEX=VALUE "
                         "(hex or decimal), e.g. 0x11=1 for the Orb of "
                         "the Hero")
    ap.add_argument("--json", type=Path)
    args = ap.parse_args()
    methods = method_table(Path(__file__).resolve().parent.parent)
    model = FormModel(args.exe, args.layout, args.form)
    entry = FORMS[args.form][1]
    model.run(methods[entry] if isinstance(entry, str) else entry)
    for item in args.state:
        idx, val = (int(x, 0) for x in item.split("="))
        model.uc.mem_write(G_STATE + 4 * idx, struct.pack("<i", val))
    for name in args.then:
        model.run(methods[name])
    b = model.bounds()
    if args.json:
        args.json.write_text(json.dumps(b, indent=1, sort_keys=True))
    for name in sorted(b, key=lambda n: (b[n][1], b[n][0])):
        if args.only and name not in args.only:
            continue
        print(f"{name:12s} left {b[name][0]:4d} top {b[name][1]:4d} "
              f"width {b[name][2]:4d} height {b[name][3]:4d}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
