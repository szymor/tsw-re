#!/usr/bin/env python3
"""Minimal native handler harness prototype (Unicorn-based).

This maps the PE image, a synthetic stack, form/VCL stubs and the game globals,
then calls a chosen event function in the original x86 code.  Presentation and
import calls are stubbed with a plain ``ret``; state/map/command-buffer effects
are captured.

This is intentionally conservative: any invalid memory access aborts the run
with a diagnostic.  It is not yet a full VCL emulator.

Run with the local venv that has unicorn installed:
    /tmp/opencode/venv/bin/python native_event_oracle.py --self-test
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

try:
    from unicorn import (Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE,
                         UC_HOOK_MEM_INVALID, UC_HOOK_MEM_FETCH_UNMAPPED)
    from unicorn.x86_const import (
        UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ECX,
        UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESI,
        UC_X86_REG_ESP, UC_X86_REG_FS_BASE,
    )
except ImportError:  # pragma: no cover - dependency guard
    Uc = None

IMAGE_BASE = 0x400000
EXPECTED_SHA256 = "41cb9844d9bdcf2641a34d4b152d6fbbb0455077abe0e9d7bfed77761a4d2779"
CODE_START = 0x401000
CODE_END = 0x488E94
STACK_BASE = 0x20000000
STACK_SIZE = 0x00100000
STUB_BASE = 0x0BAD0000
STUB_SIZE = 0x2000
RETURN_SENTINEL = STUB_BASE + 0x1000
FORM_BASE = 0x10000000
FORM_SIZE = 0x1000
OBJECT_BASE = 0x10001000
OBJECT_SIZE = 0x1000
FS_BASE = 0x30000000
GLOBAL_BASE = 0x4B8000
GLOBAL_SIZE = 0x3000

STATE_BASE = 0x4B8688
MAP_BASE = 0x4B8934
COUNT_ADDR = 0x48C5AC
CMD_BASE = 0x48C74C
FLOOR_ADDR = 0x4B8698
FLOOR_X = 0x4B86A0
FLOOR_Y = 0x4B86A4
SOUND_ADDR = 0x4B87F0
AUX_ADDRS = {
    "target_index": 0x48C584,
    "direction": 0x48C590,
    "sprite": 0x48C58C,
    "message_selector": 0x48C5A0,
    "event_selector": 0x48C55C,
    "current_monster": 0x48C5B8,
    "monster_hp": 0x48C5BC,
}
STATE_SOUND_INDEX = 0x5A
STATE_X_INDEX = 0x18 // 4
STATE_Y_INDEX = 0x1C // 4


def align_down(value: int, size: int) -> int:
    return value & ~(size - 1)


def parse_pe(path: Path):
    data = path.read_bytes()
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[e_lfanew:e_lfanew + 4] == b"PE\0\0"
    coff = e_lfanew + 4
    number_sections = struct.unpack_from("<H", data, coff + 2)[0]
    optional_size = struct.unpack_from("<H", data, coff + 16)[0]
    optional = coff + 20
    image_base = struct.unpack_from("<I", data, optional + 0x1C)[0]
    section_table = optional + optional_size
    sections = []
    for i in range(number_sections):
        off = section_table + i * 40
        name = data[off:off + 8].rstrip(b"\0").decode("ascii", "replace")
        v_size, v_addr, raw_size, raw_ptr = struct.unpack_from("<IIII", data, off + 8)
        sections.append({
            "name": name, "v_size": v_size, "v_addr": v_addr,
            "raw_size": raw_size, "raw_ptr": raw_ptr,
        })
    return data, image_base, sections


@dataclass
class RunResult:
    entry: int
    steps: int
    ok: bool
    termination: str = ""
    tainted: bool = False
    error: str = ""
    stub_calls: list[int] = field(default_factory=list)
    command_count: int = 0
    commands: list[tuple[int, int, int]] = field(default_factory=list)
    command_truncated: bool = False
    state: bytes = b""
    map_block: bytes = b""
    digest: str = ""


class NativeHarness:
    def __init__(self, exe: Path, allow_stubs: bool = False,
                 allow_other: bool = False):
        if Uc is None:
            raise RuntimeError("unicorn is not installed in this interpreter")
        digest = hashlib.sha256(exe.read_bytes()).hexdigest()
        if digest != EXPECTED_SHA256 and not allow_other:
            raise ValueError(f"binary hash {digest} does not match {EXPECTED_SHA256}")
        self.digest = digest
        self.allow_stubs = allow_stubs
        self.baseline_tainted = allow_stubs or digest != EXPECTED_SHA256
        self.used = False
        self.termination = ""
        self.tainted = False
        self.data, self.image_base, self.sections = parse_pe(exe)
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.stub_calls: list[int] = []
        self.steps = 0
        self._map_image()
        self._install_hooks()

    def _map_image(self):
        uc = self.uc
        for section in self.sections:
            va = self.image_base + section["v_addr"]
            size = max(section["v_size"], section["raw_size"])
            if size == 0:
                continue
            start = align_down(va, 0x1000)
            end = align_down(va + size + 0xFFF, 0x1000)
            uc.mem_map(start, end - start, 7)
            raw = self.data[section["raw_ptr"]:section["raw_ptr"] + section["raw_size"]]
            if raw:
                uc.mem_write(va, raw)
        # BSS/global area is covered by the PE BSS section mapping.
        uc.mem_map(STACK_BASE, STACK_SIZE, 7)
        uc.mem_map(STUB_BASE, STUB_SIZE, 7)
        uc.mem_write(STUB_BASE, b"\xC3" * STUB_SIZE)
        uc.mem_map(FORM_BASE, FORM_SIZE, 7)
        uc.mem_map(OBJECT_BASE, OBJECT_SIZE, 7)
        uc.mem_map(FS_BASE, 0x1000, 7)
        uc.mem_write(FS_BASE, (0xFFFFFFFF).to_bytes(4, "little"))
        # Unicorn 2.1's FS_BASE register is a no-op; map the null page so the
        # Delphi SEH prologue's fs:[0] access resolves.
        uc.mem_map(0x0, 0x1000, 7)
        uc.mem_write(0x0, (0xFFFFFFFF).to_bytes(4, "little"))
        uc.reg_write(UC_X86_REG_FS_BASE, FS_BASE)
        # Every form pointer slot points at a dummy object; the map-display
        # slot is then specialised below.
        dummy = (OBJECT_BASE).to_bytes(4, "little") * (FORM_SIZE // 4)
        uc.mem_write(FORM_BASE, dummy)
        uc.mem_write(OBJECT_BASE, (STUB_BASE).to_bytes(4, "little") * 64)
        # Map display object at OBJECT_BASE: width/height at +0x2c/+0x30.
        uc.mem_write(OBJECT_BASE + 0x2C, (32).to_bytes(4, "little"))
        uc.mem_write(OBJECT_BASE + 0x30, (32).to_bytes(4, "little"))
        # Form[0x95] = map display object (slot index 0x95 -> byte 0x254).
        uc.mem_write(FORM_BASE + 0x95 * 4, (OBJECT_BASE).to_bytes(4, "little"))

    def _install_hooks(self):
        uc = self.uc

        def on_code(uc, address, size, user_data):
            self.steps += 1
            if address == STUB_BASE:
                if not self.allow_stubs:
                    self.termination = "unsupported_transfer"
                    self.tainted = True
                    uc.emu_stop()
                    return
                self.tainted = True
            if self.steps > 2_000_000:
                self.termination = "step_limit"
                uc.emu_stop()
                return
            # Direct call outside the executable image.
            try:
                opcode = uc.mem_read(address, 1)[0]
            except Exception:
                return
            if opcode == 0xE8 and size == 5:
                rel = struct.unpack("<i", bytes(uc.mem_read(address + 1, 4)))[0]
                target = (address + 5 + rel) & 0xFFFFFFFF
                if not (CODE_START <= target < CODE_END):
                    self.stub_calls.append(target)
                    if not self.allow_stubs:
                        self.termination = "unsupported_call"
                        uc.emu_stop()
                        return
                    self.tainted = True
                    uc.reg_write(UC_X86_REG_EAX, 0)
                    uc.reg_write(UC_X86_REG_EIP, address + 5)
            elif opcode == 0xE9 and size == 5:
                rel = struct.unpack("<i", bytes(uc.mem_read(address + 1, 4)))[0]
                target = (address + 5 + rel) & 0xFFFFFFFF
                if not (CODE_START <= target < CODE_END):
                    self.stub_calls.append(target)
                    self.termination = "external_jump"
                    self.tainted = True
                    uc.emu_stop()

        def on_invalid(uc, access, address, size, value, user_data):
            self.invalid_addr = f"invalid access addr=0x{address:08x} size={size}"
            uc.emu_stop()

        def on_fetch(uc, access, address, size, value, user_data):
            self.stub_calls.append(address)
            self.tainted = True
            if not self.allow_stubs:
                self.termination = "unsupported_transfer"
                uc.emu_stop()
                return
            esp = uc.reg_read(UC_X86_REG_ESP)
            try:
                ret = struct.unpack("<I", bytes(uc.mem_read(esp, 4)))[0]
            except Exception:
                self.termination = "bad_stub_return"
                uc.emu_stop()
                return
            uc.reg_write(UC_X86_REG_ESP, esp + 4)
            uc.reg_write(UC_X86_REG_EAX, 0)
            uc.reg_write(UC_X86_REG_EIP, ret)

        uc.hook_add(UC_HOOK_CODE, on_code)
        uc.hook_add(UC_HOOK_MEM_FETCH_UNMAPPED, on_fetch)

    def _reset_stack(self):
        esp = STACK_BASE + STACK_SIZE - 0x1000
        self.uc.reg_write(UC_X86_REG_ESP, esp)
        # Return address to a stub that stops execution.
        self.uc.mem_write(esp, (RETURN_SENTINEL).to_bytes(4, "little"))

    def _write_state(self, state: list[int], map_block: bytes, floor: int,
                     aux: dict[str, int] | None = None):
        aux = aux or {}
        self.uc.mem_write(STATE_BASE, struct.pack("<171i", *state))
        self.uc.mem_write(MAP_BASE, map_block)
        self.uc.mem_write(FLOOR_ADDR, floor.to_bytes(4, "little"))
        x = int(aux["x"]) if "x" in aux else int(state[STATE_X_INDEX])
        y = int(aux["y"]) if "y" in aux else int(state[STATE_Y_INDEX])
        sound = (int(aux["sound_state"]) if "sound_state" in aux
                 else int(state[STATE_SOUND_INDEX]))
        self.uc.mem_write(FLOOR_X, x.to_bytes(4, "little"))
        self.uc.mem_write(FLOOR_Y, y.to_bytes(4, "little"))
        self.uc.mem_write(SOUND_ADDR, sound.to_bytes(4, "little"))
        for name, addr in AUX_ADDRS.items():
            if name in aux:
                self.uc.mem_write(addr, int(aux[name]).to_bytes(4, "little"))
        self.uc.mem_write(COUNT_ADDR, (0).to_bytes(4, "little"))
        self.uc.mem_write(CMD_BASE, b"\0" * 0x1000)

    def call(self, entry: int, state: list[int], map_block: bytes, floor: int,
             aux: dict[str, int] | None = None, form: int = FORM_BASE) -> RunResult:
        if self.used:
            raise RuntimeError("NativeHarness is single-use; create a new instance")
        self.used = True
        self._reset_stack()
        self._write_state(state, map_block, floor, aux)
        self.steps = 0
        self.stub_calls = []
        self.error = ""
        self.invalid_addr = ""
        self.termination = ""
        self.tainted = self.baseline_tainted
        uc = self.uc
        uc.reg_write(UC_X86_REG_EAX, form)
        for reg in (UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
                    UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP):
            uc.reg_write(reg, 0)
        uc.reg_write(UC_X86_REG_EIP, entry)
        try:
            uc.emu_start(entry, RETURN_SENTINEL, count=2_000_000)
        except Exception as exc:
            try:
                eip = uc.reg_read(UC_X86_REG_EIP)
                esp = uc.reg_read(UC_X86_REG_ESP)
            except Exception:
                eip = esp = 0
            self.termination = "fault"
            self.error = f"{exc} eip=0x{eip:08x} esp=0x{esp:08x} {getattr(self, 'invalid_addr', '')}"
        eip = uc.reg_read(UC_X86_REG_EIP)
        if not self.termination:
            if eip == RETURN_SENTINEL:
                self.termination = "returned"
            elif self.steps >= 2_000_000:
                self.termination = "step_limit"
            else:
                self.termination = "stopped_other"
        returned = (eip == RETURN_SENTINEL and self.termination == "returned")
        count = struct.unpack("<I", bytes(uc.mem_read(COUNT_ADDR, 4)))[0]
        capture = min(count, 1024)
        commands = []
        for i in range(1, capture + 1):
            raw = bytes(uc.mem_read(CMD_BASE + i * 6, 6))
            commands.append(struct.unpack("<HHH", raw))
        state_bytes = bytes(uc.mem_read(STATE_BASE, 684))
        map_bytes = bytes(uc.mem_read(MAP_BASE, 6273))
        ok = returned and not self.tainted and not self.error
        return RunResult(entry, self.steps, ok, self.termination,
                         self.tainted, self.error, list(self.stub_calls),
                         count, commands, count > capture, state_bytes,
                         map_bytes, self.digest)


def self_test(exe: Path) -> int:
    harness = NativeHarness(exe)
    state = [0] * 171
    state[0] = 1000
    state[1] = 100
    state[2] = 100
    state[0x010 // 4] = 1
    result = harness.call(0x455CB8, state, bytes(6273), 1)  # syokidata
    print(f"syokidata: ok={result.ok} steps={result.steps} error={result.error}")
    print(f"stub calls: {len(result.stub_calls)}")
    return 0 if result.ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("exe", nargs="?", type=Path)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    exe = args.exe or Path.home() / ".wine/drive_c/Program Files (x86)/Tower of the Sorcerer/TSW.exe"
    if Uc is None:
        print("error: run with an interpreter that has unicorn installed", file=sys.stderr)
        return 2
    if args.self_test:
        return self_test(exe)
    print("no mode selected; use --self-test")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
