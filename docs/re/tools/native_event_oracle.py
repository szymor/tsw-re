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
import collections
import hashlib
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

import event_snapshot

try:
    from unicorn import (Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE,
                         UC_HOOK_MEM_INVALID, UC_HOOK_MEM_FETCH_UNMAPPED,
                         UC_HOOK_MEM_READ_UNMAPPED, UC_HOOK_MEM_WRITE_UNMAPPED)
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
STUB_SIZE = 0x1000
VTABLE_BASE = 0x0BB00000
VTABLE_SIZE = 0x1000
RETURN_SENTINEL = 0x0BAE0000
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
    "ui_mode": 0x48C5CE,
    "message_selector": 0x48C5A0,
    "event_selector": 0x48C55C,
    "event_sprite": 0x48C558,
    "event_param": 0x48C560,
    "roujin_aux": 0x48C5A4,
    "roujin_selector": 0x48C564,
    "ui_selector": 0x48C5C8,
    "current_monster": 0x48C5B8,
    "monster_hp": 0x48C5BC,
    # Merchant/altar choice (syounin 0x449014, Button2Click 0x44e1cc).
    "shop_price": 0x48C594,
    "shop_flag": 0x48C598,       # state index of the "bought" flag
    "shop_talk_flag": 0x48C59C,  # state index of the "talked after" flag
    "choice_mode": 0x48C5CC,     # word: 0 merchant, 1 altar
    "battle_turns": 0x48C56C,    # g_battle_turns; syounin: message + 1
    # Item-use confirm case: img8work..img17work set it, Button38Click
    # (0x450ba0) switches on it.
    "confirm_case": 0x48C574,
}
# Width of an auxiliary in bytes (default 4).  0x48c5cc is a word: the
# dword at 0x48c5ce is ui_mode.
AUX_SIZES = {"choice_mode": 2}
AUX_DEFAULTS = {name: int(event_snapshot.AUX_DEFAULTS.get(name, 0))
                for name in AUX_ADDRS}
STATE_SOUND_INDEX = 0x5A
STATE_X_INDEX = 0x18 // 4
STATE_Y_INDEX = 0x1C // 4

# Call targets in these ranges are event/game logic we want to execute.  Calls
# outside them are Delphi runtime/VCL/import code and are stubbed for the
# exploratory builder capture.
ALLOWED_CALL_RANGES = [
    (0x444000, 0x44F000),  # handan, idou, disp, NPCs, battle helpers
    (0x450000, 0x456000),  # item live, imagework, syokidata
    (0x460000, 0x467000),  # mevent, zenolivework, openning, doorstackin
    (0x467000, 0x46C000),  # moncheck floor cases (0x4664e4..0x46cb1c)
    (0x46C000, 0x477000),  # ichicheck + floor handlers
    (0x478000, 0x483000),  # yousei, openning9, monidou
    (0x442C38, 0x442D4F),  # mhyouji: map redraw, also clears state[0xa6]
    (0x443648, 0x443807),  # yusyahyouji: player draw (op 0x0b/8)
    (0x4770E0, 0x478F0B),  # dooropen (op 0x0d): writes the door tile
    (0x442D50, 0x44311F),  # buttonfocus, kaidanwork, kaidanup/downcheck (stairs)
    (0x44F26C, 0x44FF10),  # yousya: one Orb-of-the-Hero row (img6work)
]


ALLOWED_EXTRA_CALLS = {
    0x402870,  # Delphi memory copy helper
    0x40352C,  # Delphi dynamic-string assignment variant
    0x403598,  # Delphi memory allocation helper
    0x40923C,  # four-dword structure copy
}

# Writable scratch object returned by object-returning presentation stubs
# (for example TBitmap.GetCanvas), inside the mapped OBJECT_BASE page, so
# callers can set properties on it (`mov [eax+0x20], ...`) harmlessly.
SCRATCH_OBJECT = 0x10001800


def is_allowed_call(target: int) -> bool:
    return (target in ALLOWED_EXTRA_CALLS or
            any(lo <= target < hi for lo, hi in ALLOWED_CALL_RANGES))


# Explicit contracts for intercepted presentation/runtime helpers.  Ghidra
# reports every entry below as a register-convention procedure (return type
# `undefined`) except the allocator helpers; the harness therefore documents
# EAX=0 as the return value and asserts that the helper does not write the
# event state, map or command buffer.
STUB_CONTRACTS = {
    0x402650: {"name": "FUN_00402650", "abi": "__register", "args": 1,
               "return": 0, "effects": "runtime_only"},
    0x4034DC: {"name": "FUN_004034dc", "abi": "__register", "args": 1,
               "return": 0, "effects": "presentation_only"},
    0x4034FC: {"name": "FUN_004034fc", "abi": "__register", "args": 2,
               "return": 0, "effects": "presentation_only"},
    0x405B6C: {"name": "FUN_00405b6c", "abi": "__register", "args": 2,
               "return": 0, "effects": "presentation_only"},
    0x410378: {"name": "FUN_00410378", "abi": "__register", "args": 2,
               "return": 0, "effects": "presentation_only"},
    0x413098: {"name": "FUN_00413098", "abi": "__register", "args": 2,
               "return": 0, "effects": "presentation_only"},
    0x4130B8: {"name": "FUN_004130b8", "abi": "__register", "args": 2,
               "return": 0, "effects": "presentation_only"},
    0x4130D8: {"name": "FUN_004130d8", "abi": "__register", "args": 2,
               "return": 0, "effects": "presentation_only"},
    0x4130F8: {"name": "FUN_004130f8", "abi": "__register", "args": 2,
               "return": 0, "effects": "presentation_only"},
    0x413500: {"name": "FUN_00413500", "abi": "__register", "args": 3,
               "return": 0, "effects": "presentation_only"},
    0x41353C: {"name": "FUN_0041353c", "abi": "__register", "args": 2,
               "return": 0, "effects": "presentation_only"},
    0x4135D4: {"name": "FUN_004135d4", "abi": "__register", "args": 2,
               "return": 0, "effects": "presentation_only"},
    0x419E4C: {"name": "FUN_00419e4c", "abi": "__register", "args": 2,
               "return": 0, "effects": "presentation_only"},
    0x41C1E8: {"name": "FUN_0041c1e8", "abi": "__register", "args": 2,
               "return": 0, "effects": "presentation_only"},
    0x41A5B8: {"name": "FUN_0041a5b8", "abi": "__register", "args": 4,
               "return": 0, "effects": "presentation_only",
               "stack_bytes": 4},  # ends in ret 4
    0x421A54: {"name": "FUN_00421a54", "abi": "__register", "args": 1,
               "return": 0, "effects": "presentation_only"},
    0x42C454: {"name": "FUN_0042c454", "abi": "__register", "args": 2,
               "return": 0, "effects": "presentation_only"},
    0x4433A4: {"name": "FUN_004433a4", "abi": "__register", "args": 1,
               "return": 0, "effects": "player_animation_only"},
    0x44CB34: {"name": "FUN_0044cb34", "abi": "__register", "args": 1,
               "return": 0, "effects": "presentation_only"},
    0x443808: {"name": "FUN_00443808", "abi": "__register", "args": 1,
               "return": 0, "effects": "player_animation_only"},
    # System_LStrCat (0x403658): string building for image/sound paths and
    # dialogue text; reallocates through the memory manager (0x403924).  It
    # writes only the destination string variable, never game state.
    0x403658: {"name": "System_LStrCat", "abi": "__register", "args": 2,
               "return": 0, "effects": "runtime_string_only"},
    # Runtime/VCL calls reached by the timeline (stackwork) and openning9;
    # register convention, <= 3 arguments (Ghidra prototypes), so no stack
    # cleanup.  None of them writes game state, the map or the timeline.
    0x40369C: {"name": "System_LStrCat3", "abi": "__register", "args": 3,
               "return": 0, "effects": "runtime_string_only"},
    0x402C34: {"name": "TObject_Free", "abi": "__register", "args": 1,
               "return": 0, "effects": "runtime_only"},
    0x41D718: {"name": "TObject_Create", "abi": "__register", "args": 3,
               "return": 0x10001800, "effects": "runtime_only"},
    0x430F20: {"name": "TMediaPlayer_Open", "abi": "__register", "args": 3,
               "return": 0, "effects": "media_only"},
    0x431188: {"name": "TMediaPlayer_Stop", "abi": "__register", "args": 3,
               "return": 0, "effects": "media_only"},
    0x431250: {"name": "TMediaPlayer_Play", "abi": "__register", "args": 2,
               "return": 0, "effects": "media_only"},
    # soundplay (0x47c2bc) setters, Ghidra-checked: plain byte stores into
    # the TMediaPlayer object, ret with no stack cleanup.
    0x431C8C: {"name": "TMediaPlayer_SetDeviceType", "abi": "__register",
               "args": 2, "return": 0, "effects": "media_only"},  # dl = 9: sequencer
    0x431C5C: {"name": "TMediaPlayer_SetNotify", "abi": "__register",
               "args": 2, "return": 0, "effects": "media_only"},
    # TControl.GetText (0x4135a4): reads a VCL control's text into a string
    # variable (zyouout appends the RichEdit text for arg1 5/0x11/0x37).  The
    # stub leaves the destination untouched (empty string).
    0x4135A4: {"name": "TControl_GetText", "abi": "__register", "args": 2,
               "return": 0, "effects": "presentation_only"},
    # File I/O reached by yousei (0x478f0c): the ranking file optionN.dat is
    # read (0x120 bytes) and the save0.dat checkpoint written.  BlockRead
    # yields zeros, i.e. no valid ranking data: the original then uses its
    # built-in temporary ranking (after ShowMessage).  None of them touches
    # game state; BlockWrite only copies the map/state out.
    0x40414D: {"name": "System_AssignFile", "abi": "__register", "args": 2,
               "return": 0, "effects": "file_only"},
    0x40458D: {"name": "System_Reset", "abi": "__register", "args": 2,
               "return": 0, "effects": "file_only"},
    0x4045A4: {"name": "System_Rewrite", "abi": "__register", "args": 2,
               "return": 0, "effects": "file_only"},
    0x4041F0: {"name": "System_BlockRead", "abi": "__register", "args": 4,
               "return": 0, "effects": "file_read_zeros", "zero_fill": True,
               "stack_bytes": 4},  # ret 4
    0x404254: {"name": "System_BlockWrite", "abi": "__register", "args": 4,
               "return": 0, "effects": "file_only", "stack_bytes": 4},
    0x4042B8: {"name": "System_CloseFile", "abi": "__register", "args": 1,
               "return": 0, "effects": "file_only"},
    0x402710: {"name": "System_IOTest", "abi": "__register", "args": 0,
               "return": 0, "effects": "runtime_only"},
    0x42CF78: {"name": "ShowMessage", "abi": "__register", "args": 1,
               "return": 0, "effects": "presentation_only"},
    # System._LStrCatN (0x403710): dest = eax, count = edx, the strings are
    # pushed by the caller and popped by the routine (lea esp,[esp+edx*4]).
    # Item pickups build the status-bar text with it.
    0x403710: {"name": "System_LStrCatN", "abi": "__register", "args": None,
               "return": 0, "effects": "runtime_string_only",
               "stack_args_edx": True},
    # TCustomForm.ShowModal (0x4237dc): Button1Click 0x1a shows the ranking
    # form on difficulty 0.  The form touches no TTSW10 state, but its OnClose
    # (form2onclose 0x43a504) re-enables TTSW10's Timer2 before ShowModal
    # returns; run_execution_oracle models that.  Returns mrOk.
    # System._FreeMem (0x402668): eax = block; releases a string/heap block
    # through the memory manager (the LStrAsg release path, 0x403567).
    0x402668: {"name": "System_FreeMem", "abi": "__register", "args": 1,
               "return": 0, "effects": "runtime_only"},
    # TMenuItem.SetChecked (0x4102f0) / SetEnabled (0x410378): eax = item,
    # dl = value.  syokidata2 re-checks the Wav/BGM items; the death prompt's
    # No enables Game>Load.  Menu state only.
    0x4102F0: {"name": "TMenuItem_SetChecked", "abi": "__register", "args": 2,
               "return": 0, "effects": "presentation_only"},
    0x410378: {"name": "TMenuItem_SetEnabled", "abi": "__register", "args": 2,
               "return": 0, "effects": "presentation_only"},
    # MessageDlg (0x42cec8): zyouout's death prompt (sub-event 103, called
    # at 0x44a3b8).  eax = text, dl = type, cx = buttons, one stack arg
    # (help context, ret 4).  The answer (mrYes = 6 / mrNo = 7) is set per
    # execution-oracle case; the default answers Yes.
    0x42CEC8: {"name": "MessageDlg", "abi": "__register", "args": 4,
               "return": 6, "effects": "presentation_only",
               "stack_bytes": 4},
    0x4237DC: {"name": "TCustomForm_ShowModal", "abi": "__register",
               "args": 1, "return": 1, "effects": "presentation_only"},
    0x42C464: {"name": "TTimer_SetInterval", "abi": "__register", "args": 2,
               "return": 0, "effects": "timing_only"},  # edx = interval
    0x42C150: {"name": "TImage_GetCanvas", "abi": "__register", "args": 3,
               "return": 0x10001800, "effects": "presentation_only"},
    0x413178: {"name": "TControl_GetClientWidth", "abi": "__register",
               "args": 1, "return": 0, "effects": "presentation_only"},
    0x4131BC: {"name": "TControl_GetClientHeight", "abi": "__register",
               "args": 1, "return": 0, "effects": "presentation_only"},
    0x4136AC: {"name": "FUN_004136ac", "abi": "__register", "args": 2,
               "return": 0, "effects": "presentation_only"},
    0x41C160: {"name": "FUN_0041c160", "abi": "__register", "args": 2,
               "return": 0, "effects": "presentation_only"},
    0x4291C8: {"name": "FUN_004291c8", "abi": "__register", "args": 2,
               "return": 0, "effects": "presentation_only"},
    # Canvas helpers reached by mhyouji/kasanegaki/itemlive (Ghidra-checked).
    0x41DAD8: {"name": "TBitmap_GetCanvas", "abi": "__register", "args": 1,
               "return": 0x10001800, "effects": "presentation_only"},
    0x417E44: {"name": "TCanvas_draw_5", "abi": "__register", "args": 5,
               "return": 0, "effects": "presentation_only",
               "stack_bytes": 8},  # 5 args: two on the stack, ret 8
    0x41A530: {"name": "TCanvas_CopyRect", "abi": "__register", "args": 4,
               "return": 0, "effects": "presentation_only",
               "stack_bytes": 4},  # ret 4
}
CLASSIFIED_STUBS = set(STUB_CONTRACTS)
PRESENTATION_RET_NAME = "presentation_ret"
PRESENTATION_RET_CONTRACT = {
    "name": PRESENTATION_RET_NAME, "abi": "__register", "args": None,
    "return": 0, "effects": "presentation_only",
}


REGISTERS = {} if Uc is None else {
    0: UC_X86_REG_EAX, 1: UC_X86_REG_ECX, 2: UC_X86_REG_EDX,
    3: UC_X86_REG_EBX, 4: UC_X86_REG_ESP, 5: UC_X86_REG_EBP,
    6: UC_X86_REG_ESI, 7: UC_X86_REG_EDI,
}


def align_down(value: int, size: int) -> int:
    return value & ~(size - 1)


def read_u32(uc, address: int) -> int:
    return struct.unpack("<I", bytes(uc.mem_read(address, 4)))[0]


ALLOWED_VTABLE_OFFSETS = {0x0C, 0x1C, 0x20, 0x28, 0x2C, 0x34, 0x38, 0x80}  # 0x34: LoadFromFile, 0x38: TStrings.Clear
X86_PREFIXES = {
    0x26, 0x2E, 0x36, 0x3E, 0x64, 0x65, 0x66, 0x67, 0xF0, 0xF2, 0xF3,
}


def decode_indirect_target(uc, address: int, size: int) -> tuple[int, str, int, int | None] | None:
    """Return (target, description, reg) for x86 FF /2 or FF /4, else None.

    The decoder covers the ModRM/SIB forms emitted by Delphi for virtual calls,
    import-table calls and switch-table jumps.  Unsupported forms return None
    and therefore fail closed in the harness.
    """
    try:
        raw = bytes(uc.mem_read(address, size))
    except Exception:
        return None
    if len(raw) < 2 or raw[0] != 0xFF:
        return None
    reg = (raw[1] >> 3) & 7
    if reg not in (2, 4):
        return None
    mod = raw[1] >> 6
    rm = raw[1] & 7
    index = 2
    if mod == 3:
        target = int(uc.reg_read(REGISTERS[rm]))
        return target, f"reg{rm}", reg, None
    base_value = 0
    index_value = 0
    scale = 1
    disp = 0
    if rm == 4:
        if index >= len(raw):
            return None
        sib = raw[index]
        index += 1
        scale = 1 << (sib >> 6)
        sib_index = (sib >> 3) & 7
        sib_base = sib & 7
        if sib_index != 4:
            index_value = int(uc.reg_read(REGISTERS[sib_index]))
        if sib_base == 5 and mod == 0:
            if index + 4 > len(raw):
                return None
            disp = int.from_bytes(raw[index:index + 4], "little")
            index += 4
        else:
            base_value = int(uc.reg_read(REGISTERS[sib_base]))
    elif rm == 5 and mod == 0:
        if index + 4 > len(raw):
            return None
        disp = int.from_bytes(raw[index:index + 4], "little")
        index += 4
    else:
        base_value = int(uc.reg_read(REGISTERS[rm]))
    if mod == 1:
        if index + 1 > len(raw):
            return None
        disp = int.from_bytes(raw[index:index + 1], "little", signed=True)
        index += 1
    elif mod == 2:
        if index + 4 > len(raw):
            return None
        disp = int.from_bytes(raw[index:index + 4], "little", signed=True)
        index += 4
    effective = (base_value + index_value * scale + disp) & 0xFFFFFFFF
    try:
        target = read_u32(uc, effective)
    except Exception:
        return None
    return target, f"mem[0x{effective:08x}]", reg, effective


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
    stub_mode: str = "strict"
    stub_invocations: list[dict[str, object]] = field(default_factory=list)
    aux: dict[str, int] = field(default_factory=dict)
    executed: list[int] = field(default_factory=list)
    executed_runs: list[list[int]] = field(default_factory=list)


class NativeHarness:
    def __init__(self, exe: Path, allow_stubs: bool = False,
                 allow_other: bool = False, presentation_stubs: bool = False):
        if Uc is None:
            raise RuntimeError("unicorn is not installed in this interpreter")
        digest = hashlib.sha256(exe.read_bytes()).hexdigest()
        if digest != EXPECTED_SHA256 and not allow_other:
            raise ValueError(f"binary hash {digest} does not match {EXPECTED_SHA256}")
        self.digest = digest
        self.allow_stubs = allow_stubs
        self.presentation_stubs = presentation_stubs
        self.baseline_tainted = allow_stubs or digest != EXPECTED_SHA256
        self.used = False
        self.termination = ""
        self.tainted = False
        self.unsupported_transfer: tuple[str, int, int] | None = None
        self.stub_invocations: list[dict[str, object]] = []
        self.recent_code: collections.deque[int] = collections.deque(maxlen=16)
        # Every executed instruction address (coverage input for vectors).
        self.executed: set[int] = set()
        # Contiguous executed runs in execution order: [start, end) pairs.
        self.executed_runs: list[list[int]] = []
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
        uc.mem_map(RETURN_SENTINEL, 0x1000, 7)
        uc.mem_write(RETURN_SENTINEL, b"\xC3")
        uc.mem_map(VTABLE_BASE, VTABLE_SIZE, 7)
        uc.mem_write(VTABLE_BASE,
                     (STUB_BASE).to_bytes(4, "little") * (VTABLE_SIZE // 4))
        uc.mem_map(FORM_BASE, FORM_SIZE, 7)
        uc.mem_map(OBJECT_BASE, OBJECT_SIZE, 7)
        uc.mem_map(FS_BASE, 0x1000, 7)
        uc.mem_write(FS_BASE, (0xFFFFFFFF).to_bytes(4, "little"))
        # Unicorn 2.1's FS_BASE register is a no-op; map the null page so the
        # Delphi SEH prologue's fs:[0] access resolves.
        uc.mem_map(0x0, 0x1000, 7)
        uc.mem_write(0x0, (0xFFFFFFFF).to_bytes(4, "little"))
        uc.reg_write(UC_X86_REG_FS_BASE, FS_BASE)
        # Every form pointer slot points at the dummy object.  The dummy
        # object has a vtable at +0 and self-pointers for its fields.
        dummy = (OBJECT_BASE).to_bytes(4, "little") * (FORM_SIZE // 4)
        uc.mem_write(FORM_BASE, dummy)
        uc.mem_write(OBJECT_BASE,
                     (OBJECT_BASE).to_bytes(4, "little") * (OBJECT_SIZE // 4))
        uc.mem_write(OBJECT_BASE, (VTABLE_BASE).to_bytes(4, "little"))
        # The scratch object returned by object-returning stubs gets the stub
        # vtable too, so virtual calls on it reach typed presentation slots.
        uc.mem_write(SCRATCH_OBJECT, (VTABLE_BASE).to_bytes(4, "little"))
        # Map display fields at +0x2c/+0x30.
        uc.mem_write(OBJECT_BASE + 0x2C, (32).to_bytes(4, "little"))
        uc.mem_write(OBJECT_BASE + 0x30, (32).to_bytes(4, "little"))
        # Form[0x95] = map display object (slot index 0x95 -> byte 0x254).
        uc.mem_write(FORM_BASE + 0x95 * 4, (OBJECT_BASE).to_bytes(4, "little"))
        # Global presentation object pointers read by zenolivework/openning2:
        # `[global] -> object -> vtable`, then virtual presentation calls.
        for addr in (0x48C514, 0x48C518, 0x48C51C, 0x48C520, 0x48C524):
            uc.mem_write(addr, (OBJECT_BASE).to_bytes(4, "little"))

    @staticmethod
    def _registers(uc) -> dict[str, int]:
        return {
            "eax": int(uc.reg_read(UC_X86_REG_EAX)),
            "ebx": int(uc.reg_read(UC_X86_REG_EBX)),
            "ecx": int(uc.reg_read(UC_X86_REG_ECX)),
            "edx": int(uc.reg_read(UC_X86_REG_EDX)),
            "esi": int(uc.reg_read(UC_X86_REG_ESI)),
            "edi": int(uc.reg_read(UC_X86_REG_EDI)),
            "esp": int(uc.reg_read(UC_X86_REG_ESP)),
        }

    def _record_stub(self, target: int, contract: dict[str, object],
                     caller: int, kind: str) -> None:
        self.stub_invocations.append({
            "target": target,
            "name": contract["name"],
            "abi": contract["abi"],
            "args": contract["args"],
            "return": contract["return"],
            "stack_bytes": contract.get("stack_bytes", 0),
            "effects": contract["effects"],
            "caller": caller,
            "kind": kind,
            "registers": self._registers(self.uc),
        })

    def _unsupported(self, kind: str, target: int, caller: int) -> None:
        self.unsupported_transfer = (kind, target, caller)
        history = " ".join(f"0x{a:08x}" for a in self.recent_code)
        self.error = (f"{kind} target=0x{target:08x} caller=0x{caller:08x} "
                      f"recent=[{history}]")
        self.termination = "unsupported_transfer"
        self.tainted = True
        self.uc.emu_stop()

    def _install_hooks(self):
        uc = self.uc

        def on_code(uc, address, size, user_data):
            self.steps += 1
            self.recent_code.append(address)
            self.executed.add(address)
            end = address + size
            if self.executed_runs and self.executed_runs[-1][1] == address:
                self.executed_runs[-1][1] = end
            else:
                self.executed_runs.append([address, end])
            if self.steps > 2_000_000:
                self.termination = "step_limit"
                uc.emu_stop()
                return
            if address < 0x1000:
                return self._unsupported("unsupported_low_transfer", address,
                                         address)
            if STUB_BASE <= address < STUB_BASE + STUB_SIZE:
                return self._unsupported("unclassified_stub_entry",
                                         address, address)
            try:
                opcode = int(uc.mem_read(address, 1)[0])
            except Exception:
                return
            prefix_len = 0
            prefixed_opcode = opcode
            while (prefixed_opcode in X86_PREFIXES and prefix_len < 15
                   and prefix_len + 1 < size):
                prefix_len += 1
                try:
                    prefixed_opcode = int(
                        uc.mem_read(address + prefix_len, 1)[0])
                except Exception:
                    return
            if prefix_len:
                reg = ((int(uc.mem_read(address + prefix_len + 1, 1)[0]) >> 3)
                       & 7) if prefixed_opcode == 0xFF else 0
                if (prefixed_opcode in (0xE8, 0xE9)
                        or (prefixed_opcode == 0xFF and reg in (2, 4))):
                    return self._unsupported("prefixed_transfer", 0, address)
            if opcode == 0xE8 and size == 5:
                rel = struct.unpack("<i", bytes(uc.mem_read(address + 1, 4)))[0]
                target = (address + 5 + rel) & 0xFFFFFFFF
                if target in STUB_CONTRACTS:
                    if self.allow_stubs:
                        self.tainted = True
                    elif not self.presentation_stubs:
                        return self._unsupported(
                            "presentation_stub_in_strict", target, address)
                    contract = STUB_CONTRACTS[target]
                    self._record_stub(target, contract, address, "direct")
                    if contract.get("zero_fill"):
                        # BlockRead from a missing/empty file: the buffer
                        # (edx, ecx bytes) reads back as zeros.
                        buf = int(uc.reg_read(UC_X86_REG_EDX))
                        n = int(uc.reg_read(UC_X86_REG_ECX)) & 0xFFFF
                        uc.mem_write(buf, b"\0" * n)
                    stack_bytes = int(contract.get("stack_bytes", 0))
                    if contract.get("stack_args_edx"):
                        stack_bytes = 4 * (int(uc.reg_read(UC_X86_REG_EDX)) & 0xFF)
                    if stack_bytes:
                        esp = int(uc.reg_read(UC_X86_REG_ESP))
                        uc.reg_write(UC_X86_REG_ESP, esp + stack_bytes)
                    uc.reg_write(UC_X86_REG_EAX, int(contract["return"]))
                    uc.reg_write(UC_X86_REG_EIP, address + 5)
                    return
                if not is_allowed_call(target):
                    if self.allow_stubs:
                        self.tainted = True
                        self.stub_calls.append(target)
                        uc.reg_write(UC_X86_REG_EAX, 0)
                        uc.reg_write(UC_X86_REG_EIP, address + 5)
                        return
                    return self._unsupported("unsupported_call", target, address)
            elif opcode == 0xE9 and size == 5:
                rel = struct.unpack("<i", bytes(uc.mem_read(address + 1, 4)))[0]
                target = (address + 5 + rel) & 0xFFFFFFFF
                if not (CODE_START <= target < CODE_END):
                    self.termination = "external_jump"
                    self.tainted = True
                    self.unsupported_transfer = ("external_jump", target, address)
                    self.error = f"external_jump target=0x{target:08x}"
                    uc.emu_stop()
                elif (not is_allowed_call(target)
                      and target not in {0x4770A1, 0x464024, 0x448FBC}):
                    self._unsupported("unclassified_jump", target, address)
            elif opcode == 0xFF:
                try:
                    second = int(uc.mem_read(address + 1, 1)[0])
                except Exception:
                    return
                if ((second >> 3) & 7) not in (2, 4):
                    return  # FF /0, /1, /3, /5, /6, /7 are not transfers
                decoded = decode_indirect_target(uc, address, size)
                if decoded is None:
                    return self._unsupported("unsupported_indirect_transfer",
                                             0, address)
                target, description, reg, effective = decoded
                if reg == 4:
                    if (CODE_START <= target < CODE_END
                            and is_allowed_call(target)):
                        return
                    if self.allow_stubs:
                        self.tainted = True
                        return
                    return self._unsupported("unsupported_indirect_jump",
                                             target, address)
                if target in STUB_CONTRACTS:
                    if self.allow_stubs:
                        self.tainted = True
                    elif not self.presentation_stubs:
                        return self._unsupported(
                            "presentation_stub_in_strict", target, address)
                    contract = STUB_CONTRACTS[target]
                    self._record_stub(target, contract, address,
                                      f"indirect:{description}")
                    if contract.get("zero_fill"):
                        # BlockRead from a missing/empty file: the buffer
                        # (edx, ecx bytes) reads back as zeros.
                        buf = int(uc.reg_read(UC_X86_REG_EDX))
                        n = int(uc.reg_read(UC_X86_REG_ECX)) & 0xFFFF
                        uc.mem_write(buf, b"\0" * n)
                    stack_bytes = int(contract.get("stack_bytes", 0))
                    if contract.get("stack_args_edx"):
                        stack_bytes = 4 * (int(uc.reg_read(UC_X86_REG_EDX)) & 0xFF)
                    if stack_bytes:
                        esp = int(uc.reg_read(UC_X86_REG_ESP))
                        uc.reg_write(UC_X86_REG_ESP, esp + stack_bytes)
                    uc.reg_write(UC_X86_REG_EAX, int(contract["return"]))
                    uc.reg_write(UC_X86_REG_EIP, address + size)
                    return
                if target == STUB_BASE:
                    if self.allow_stubs:
                        self.tainted = True
                    elif not self.presentation_stubs:
                        return self._unsupported(
                            "presentation_stub_in_strict", target, address)
                    offset = (None if effective is None
                              else effective - VTABLE_BASE)
                    if offset not in ALLOWED_VTABLE_OFFSETS:
                        return self._unsupported(
                            "unclassified_vtable_slot", target, address)
                    self._record_stub(target, PRESENTATION_RET_CONTRACT,
                                      address, f"indirect:{description}")
                    uc.reg_write(UC_X86_REG_EAX, 0)
                    uc.reg_write(UC_X86_REG_EIP, address + size)
                    return
                if not is_allowed_call(target):
                    if self.allow_stubs:
                        self.tainted = True
                        self.stub_calls.append(target)
                        uc.reg_write(UC_X86_REG_EAX, 0)
                        uc.reg_write(UC_X86_REG_EIP, address + size)
                        return
                    return self._unsupported("unsupported_indirect_call",
                                             target, address)

        def on_invalid(uc, access, address, size, value, user_data):
            self.invalid_addr = f"invalid access addr=0x{address:08x} size={size}"
            uc.emu_stop()

        def on_fetch(uc, access, address, size, value, user_data):
            if not self.allow_stubs:
                return self._unsupported("unsupported_fetch", address, address)
            self.stub_calls.append(address)
            self.tainted = True
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

        def on_unmapped(uc, access, address, size, value, user_data):
            page = address & ~0xFFF
            if not self.allow_stubs:
                self.termination = "unmapped_data"
                uc.emu_stop()
                return False
            try:
                uc.mem_map(page, 0x1000, 7)
            except Exception:
                self.termination = "unmapped_data"
                uc.emu_stop()
                return False
            self.tainted = True
            return True

        uc.hook_add(UC_HOOK_CODE, on_code)
        uc.hook_add(UC_HOOK_MEM_FETCH_UNMAPPED, on_fetch)
        uc.hook_add(UC_HOOK_MEM_READ_UNMAPPED, on_unmapped)
        uc.hook_add(UC_HOOK_MEM_WRITE_UNMAPPED, on_unmapped)

    def _reset_stack(self):
        esp = STACK_BASE + STACK_SIZE - 0x1000
        self.uc.reg_write(UC_X86_REG_ESP, esp)
        # Return address to a stub that stops execution.
        self.uc.mem_write(esp, (RETURN_SENTINEL).to_bytes(4, "little"))

    def _write_state(self, state: list[int], map_block: bytes, floor: int,
                     aux: dict[str, int] | None = None,
                     commands: list[tuple[int, int, int]] | None = None,
                     count: int | None = None):
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
            value = int(aux.get(name, AUX_DEFAULTS[name]))
            size = AUX_SIZES.get(name, 4)
            self.uc.mem_write(addr, (value & ((1 << (8 * size)) - 1))
                              .to_bytes(size, "little"))
        self.uc.mem_write(COUNT_ADDR, (0).to_bytes(4, "little"))
        self.uc.mem_write(CMD_BASE, b"\0" * 0x1000)
        if commands:
            for i, (op, a1, a2) in enumerate(commands, start=1):
                self.uc.mem_write(
                    CMD_BASE + i * 6,
                    struct.pack("<HHH", op & 0xffff, a1 & 0xffff,
                                a2 & 0xffff))
            if count is None:
                count = len(commands)
        self.uc.mem_write(COUNT_ADDR,
                          ((count or 0) & 0xffffffff).to_bytes(4, "little"))

    def call(self, entry: int, state: list[int], map_block: bytes, floor: int,
             aux: dict[str, int] | None = None, form: int = FORM_BASE,
             commands: list[tuple[int, int, int]] | None = None,
             count: int | None = None) -> RunResult:
        if self.used:
            raise RuntimeError("NativeHarness is single-use; create a new instance")
        self.used = True
        self._reset_stack()
        self._write_state(state, map_block, floor, aux, commands, count)
        self.steps = 0
        self.stub_calls = []
        self.error = ""
        self.invalid_addr = ""
        self.termination = ""
        self.tainted = self.baseline_tainted
        self.unsupported_transfer = None
        self.stub_invocations = []
        self.recent_code.clear()
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
        capture = min(count, 4096)   # the remake VM capacity (EVENT_VM_CAPACITY)
        commands = []
        for i in range(1, capture + 1):
            raw = bytes(uc.mem_read(CMD_BASE + i * 6, 6))
            commands.append(struct.unpack("<HHH", raw))
        state_bytes = bytes(uc.mem_read(STATE_BASE, 684))
        map_bytes = bytes(uc.mem_read(MAP_BASE, 6273))
        aux_final = {
            name: int.from_bytes(bytes(uc.mem_read(addr, AUX_SIZES.get(name, 4))),
                                 "little")
            for name, addr in AUX_ADDRS.items()
        }
        truncated = count > capture
        ok = (returned and not self.tainted and not self.error
              and not truncated)
        mode = ("permissive" if self.allow_stubs else
                "presentation" if self.presentation_stubs else "strict")
        return RunResult(entry, self.steps, ok, self.termination,
                         self.tainted, self.error, list(self.stub_calls),
                         count, commands, truncated, state_bytes,
                         map_bytes, self.digest, mode,
                         list(self.stub_invocations), aux_final,
                         sorted(self.executed),
                         [list(run) for run in self.executed_runs])


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
