#!/usr/bin/env python3
"""Inventory every access to the original event command buffer.

The event system stores its command count at 0x48c5ac and its 6-byte
commands at 0x48c74c.  Handlers append commands through one of two forms:

* the ``inc [count]`` / ``[esi+eax*2+...]`` macro (the floor handlers), or
* direct absolute stores to the command buffer (for example ``roujin``).

This tool scans the whole CODE section, not just the ichicheck window.  It
uses a basic-block dataflow pass over direct branches and decoded
``jmp DWORD PTR [reg*4+table]`` switches, merges pointer provenance at joins,
and resynchronises objdump at method/handler/jump-table entries.  A fallback
seeds otherwise-unreached case blocks in methods whose entry establishes a
buffer base.  The output marks fallback-derived accesses as ``...+fallback``.

Generated output contains addresses from the user's own executable and is not
committed.  The default output path is under ``$XDG_CACHE_HOME/tsw``.

Usage:
    python3 docs/re/tools/scan_event_buffers.py /path/to/TSW.exe
    python3 docs/re/tools/scan_event_buffers.py /path/to/TSW.exe --summary
    python3 docs/re/tools/scan_event_buffers.py /path/to/TSW.exe -o /tmp/writers.tsv
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import os
import re
import subprocess
import sys
from pathlib import Path

COUNT_ADDR = 0x48C5AC
CMD_BASE = 0x48C74C
EXPECTED_SHA256 = "41cb9844d9bdcf2641a34d4b152d6fbbb0455077abe0e9d7bfed77761a4d2779"

# Addresses used by the timeline code.  The negative offsets are the
# previous-command fields read by opcodes 0x09, 0x0e and 0x15.
TARGETS = {
    COUNT_ADDR: ("count", "count"),
    CMD_BASE - 6: ("cmd_opcode_prev", "previous command opcode"),
    CMD_BASE - 4: ("cmd_arg1_prev", "previous command arg1"),
    CMD_BASE - 2: ("cmd_arg2_prev", "previous command arg2"),
    CMD_BASE + 0: ("cmd_opcode", "current command opcode"),
    CMD_BASE + 2: ("cmd_arg1", "current command arg1"),
    CMD_BASE + 4: ("cmd_arg2", "current command arg2"),
    CMD_BASE + 10: ("cursor_plus_10", "cursor-relative +0xa (current arg2 under the 0xb/10 decrement)"),
}

# ichicheck's jump-table targets are interior basic-block entries, not
# separate published methods.  Add them as pseudo-methods so the inventory
# attributes floor-handler accesses to the right case window.
EVENT_HANDLERS = [
    (0x46CBC7, "floor10"),
    (0x46F82E, "floor15"),
    (0x46F8F4, "floor20"),
    (0x4704EB, "floor24"),
    (0x4727B2, "floor25"),
    (0x4728EF, "floor30"),
    (0x472A2C, "floor32"),
    (0x4735A4, "floor33"),
    (0x47382C, "floor38"),
    (0x473961, "floor40"),
    (0x475FBA, "floor42"),
    (0x47687E, "floor43"),
    (0x476CDD, "floor48"),
    (0x476DC0, "floor49"),
    (0x4770A1, "ichicheck.end"),
]

# Non-code blobs inside otherwise contiguous handler ranges.  These are
# currently just the string/constant blob following openning31; keep the
# list explicit so it is visible and hash-bound.
KNOWN_DATA_RANGES = [(0x4658D0, 0x465900)]

BUFFER_ADDRS = set(TARGETS)

REG32 = {
    "eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp",
}
REG16 = {"ax", "bx", "cx", "dx", "si", "di", "bp", "sp"}
REG8 = {"al", "ah", "bl", "bh", "cl", "ch", "dl", "dh"}
REG_ALIASES = {r: r[1:] if r[0] in "abcd" else r for r in REG16}
REG_ALIASES.update({r: r for r in REG32})
CANON = {
    "eax": "eax", "ax": "eax", "al": "eax", "ah": "eax",
    "ebx": "ebx", "bx": "ebx", "bl": "ebx", "bh": "ebx",
    "ecx": "ecx", "cx": "ecx", "cl": "ecx", "ch": "ecx",
    "edx": "edx", "dx": "edx", "dl": "edx", "dh": "edx",
    "esi": "esi", "si": "esi", "edi": "edi", "di": "edi",
    "ebp": "ebp", "bp": "ebp", "esp": "esp", "sp": "esp",
}

# A register holds a may-set of (origin, offset, fallback) pointer entries.
# ``origin + offset`` is the current absolute address; ``offset is None``
# means the pointer is buffer-derived but no exact offset is known.
Provenance = frozenset[tuple[int, int | None, bool]]

MAX_OFFSETS = 8


def widen_provenance(entries) -> Provenance:
    """Bound pointer offsets so pointer-adjusting loops reach a fixed point.

    More than ``MAX_OFFSETS`` distinct offsets for one origin, or any unknown
    offset, collapse that origin to unknown-offset provenance.  Fallback is
    retained only when every contributing path is fallback-derived.
    """
    by_origin: dict[int, list[tuple[int | None, bool]]] = {}
    for origin, offset, fallback in entries:
        by_origin.setdefault(origin, []).append((offset, fallback))
    out: set[tuple[int, int | None, bool]] = set()
    for origin, items in by_origin.items():
        offsets = {offset for offset, _ in items if offset is not None}
        if any(offset is None for offset, _ in items) or len(offsets) > MAX_OFFSETS:
            out.add((origin, None, all(fallback for _, fallback in items)))
        else:
            by_offset: dict[int, list[bool]] = {}
            for offset, fallback in items:
                if offset is not None:
                    by_offset.setdefault(offset, []).append(fallback)
            for offset, fallbacks in by_offset.items():
                out.add((origin, offset, all(fallbacks)))
    return frozenset(out)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def load_methods(path: Path) -> list[tuple[int, str]]:
    methods: list[tuple[int, str]] = []
    if not path.exists():
        return methods
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("index"):
            continue
        parts = line.split("\t")
        if len(parts) < 4:
            continue
        try:
            addr = int(parts[1], 16)
        except ValueError:
            continue
        methods.append((addr, parts[3]))
    methods.sort()
    return methods


def nearest_method(methods: list[tuple[int, str]], addr: int) -> str:
    lo, hi = 0, len(methods)
    while lo < hi:
        mid = (lo + hi) // 2
        if methods[mid][0] <= addr:
            lo = mid + 1
        else:
            hi = mid
    if lo == 0:
        return "?"
    return f"{methods[lo - 1][1]}@{methods[lo - 1][0]:08x}"


def _parse_objdump(out: str) -> list[tuple[int, str]]:
    insns: list[tuple[int, str]] = []
    for line in out.splitlines():
        m = re.match(r"\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)", line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        text = re.sub(r"\s+", " ", m.group(3).strip())
        insns.append((addr, text))
    return insns


def _objdump_range(exe: Path, start: int, stop: int) -> list[tuple[int, str]]:
    out = subprocess.check_output(
        ["objdump", "-d", "-M", "intel",
         f"--start-address=0x{start:x}", f"--stop-address=0x{stop:x}", str(exe)],
        text=True,
        errors="replace",
    )
    return _parse_objdump(out)


def disassemble(exe: Path, entry_addrs: set[int] | None = None) -> list[tuple[int, str]]:
    out = subprocess.check_output(
        ["objdump", "-d", "-M", "intel", str(exe)],
        text=True,
        errors="replace",
    )
    merged = {addr: text for addr, text in _parse_objdump(out)}

    # A linear objdump sweep can start in the middle of a jump table and miss
    # a later entry address.  Resynchronise at known method/handler entries
    # that are absent from the linear sweep.
    if entry_addrs:
        entries = sorted(entry_addrs)
        starts = set(merged)
        for i, start in enumerate(entries):
            if start in starts:
                continue
            stop = entries[i + 1] if i + 1 < len(entries) else 0x488E94
            for addr in [a for a in merged if start <= a < stop]:
                del merged[addr]
            for addr, text in _objdump_range(exe, start, stop):
                merged[addr] = text
    return sorted(merged.items())


def split_operands(text: str) -> tuple[str, str | None]:
    if " " not in text:
        return text, None
    op, rest = text.split(" ", 1)
    # Split only on the first comma outside brackets.  objdump's Intel syntax
    # does not use nested commas in these instructions except in call lists.
    depth = 0
    for i, ch in enumerate(rest):
        if ch == "[":
            depth += 1
        elif ch == "]":
            depth -= 1
        elif ch == "," and depth == 0:
            return op, rest[:i].strip() + "|" + rest[i + 1:].strip()
    return op, rest.strip()


def dest_src(text: str) -> tuple[str | None, str | None]:
    if " " not in text:
        return None, None
    _, rest = text.split(" ", 1)
    depth = 0
    for i, ch in enumerate(rest):
        if ch == "[":
            depth += 1
        elif ch == "]":
            depth -= 1
        elif ch == "," and depth == 0:
            return rest[:i].strip(), rest[i + 1:].strip()
    return rest.strip(), None


def is_memory(operand: str | None) -> bool:
    if not operand:
        return False
    if "[" in operand:
        return True
    # objdump also prints absolute operands without brackets, e.g.
    # ``mov WORD PTR ds:0x48c74c,0x1`` or ``mov ds:0x48c5ac,eax``.
    if re.search(r"(?:byte|word|dword|qword)\s+ptr\s+(?:ds:)?0x[0-9a-f]+",
                 operand, re.IGNORECASE):
        return True
    return bool(re.search(r"(?:ds|es|ss|cs|fs|gs):0x[0-9a-f]+",
                          operand, re.IGNORECASE))


def target_for(addr: int) -> tuple[str, str] | None:
    return TARGETS.get(addr)


def memory_base_disp(operand: str) -> tuple[str | None, int | None, bool]:
    """Return (canonical register, displacement or None, has_index)."""
    m = re.search(r"\[([^\]]+)\]", operand)
    if not m:
        return None, None, False
    expr = m.group(1)
    # Remove segment prefixes.
    expr = re.sub(r"^(?:ds|es|ss|cs|fs|gs):", "", expr, flags=re.IGNORECASE)
    parts = [p.strip() for p in re.split(r"([+-])", expr) if p.strip()]
    reg = None
    disp = 0
    has_index = "*" in expr
    sign = 1
    for part in parts:
        if part in ("+", "-"):
            sign = 1 if part == "+" else -1
            continue
        if part in CANON:
            if reg is None:
                reg = CANON[part]
            else:
                has_index = True
        elif re.match(r"^0x[0-9a-f]+$", part):
            disp += sign * int(part, 16)
        elif re.match(r"^[0-9]+$", part):
            disp += sign * int(part, 10)
        else:
            # e.g. eax*2
            mm = re.match(r"^([a-z]+)\*(\d+)$", part)
            if mm:
                has_index = True
                if mm.group(1) in CANON and reg is None:
                    reg = CANON[mm.group(1)]
            else:
                has_index = True
    if reg is not None and not has_index:
        return reg, disp, False
    return reg, disp if has_index or reg is not None else None, has_index


def apply_instruction(text: str, regs: dict[str, Provenance]) -> None:
    op, operands = split_operands(text)
    dest, src = dest_src(text)
    empty: Provenance = frozenset()
    if op == "lea" and dest in CANON and src and "[" in src:
        reg, disp, has_index = memory_base_disp(src)
        c = CANON[dest]
        prov = regs.get(reg, empty) if reg else empty
        if not prov:
            regs[c] = empty
        elif has_index or disp is None:
            regs[c] = widen_provenance(
                (origin, None, fallback)
                for origin, offset, fallback in prov
            )
        else:
            regs[c] = widen_provenance(
                (origin, None if offset is None else offset + disp, fallback)
                for origin, offset, fallback in prov
            )
        return
    if op == "mov" and dest and src:
        if dest in CANON:
            c = CANON[dest]
            if re.match(r"^0x[0-9a-f]+$", src):
                value = int(src, 16)
                regs[c] = (frozenset({(value, 0, False)})
                           if value in BUFFER_ADDRS else empty)
            elif re.match(r"^[0-9]+$", src):
                value = int(src, 10)
                regs[c] = (frozenset({(value, 0, False)})
                           if value in BUFFER_ADDRS else empty)
            elif src in CANON:
                regs[c] = regs.get(CANON[src], empty)
            else:
                regs[c] = empty
        return
    if op == "xor" and dest and src and dest == src and dest in CANON:
        regs[CANON[dest]] = empty
        return
    if op in ("add", "sub") and dest in CANON and src:
        c = CANON[dest]
        prov = regs.get(c, empty)
        if not prov:
            regs[c] = empty
        elif not re.match(r"^0x[0-9a-f]+$", src):
            # Register/memory-valued adjustment: keep the origin but lose the
            # exact offset instead of dropping provenance entirely.
            regs[c] = widen_provenance(
                (origin, None, fallback)
                for origin, _offset, fallback in prov
            )
        else:
            delta = int(src, 16)
            if op == "sub":
                delta = -delta
            regs[c] = widen_provenance(
                (origin, None if offset is None else offset + delta, fallback)
                for origin, offset, fallback in prov
            )
        return
    if op in ("inc", "dec") and dest in CANON:
        c = CANON[dest]
        prov = regs.get(c, empty)
        delta = 1 if op == "inc" else -1
        if prov:
            regs[c] = widen_provenance(
                (origin, None if offset is None else offset + delta, fallback)
                for origin, offset, fallback in prov
            )
        else:
            regs[c] = empty
        return
    if op == "call":
        for r in ("eax", "ecx", "edx"):
            regs[r] = empty
        return
    # Other operations may write their destination; invalidate only when the
    # destination is a register.
    if dest in CANON and op not in ("cmp", "test", "push", "jmp", "je", "jne",
                                    "ja", "jae", "jb", "jbe", "jg", "jge",
                                    "jl", "jle", "jz", "jnz"):
        regs[CANON[dest]] = empty


COND_BRANCHES = {
    "je", "jne", "ja", "jae", "jb", "jbe", "jg", "jge", "jl", "jle",
    "jz", "jnz", "js", "jns", "jo", "jno", "jp", "jnp", "jc", "jnc",
    "jecxz", "loop", "loope", "loopne",
}
NO_FALLTHROUGH = {"jmp", "ret", "retn", "retf", "iretd"}


def direct_target(text: str) -> int | None:
    m = re.search(r"\s0x([0-9a-f]+)$", text)
    if m:
        return int(m.group(1), 16)
    return None


def _read_u32(data: bytes, addr: int) -> int | None:
    off = addr - 0x401000 + 0x400
    if off < 0 or off + 4 > len(data):
        return None
    return int.from_bytes(data[off:off + 4], "little")


def decode_jump_table(data: bytes, base: int, limit: int = 256) -> list[int]:
    """Read code pointers until one leaves the CODE section."""
    targets = []
    for i in range(limit):
        value = _read_u32(data, base + i * 4)
        if value is None or not (0x401000 <= value < 0x488E94):
            break
        targets.append(value)
    return targets


def recover_indirect_edges(exe: Path, insns: list[tuple[int, str]]):
    """Recover event-style ``jmp DWORD PTR [reg*4+table]`` switches.

    Returns ``(edges, data_ranges)`` where ``data_ranges`` covers each decoded
    pointer table so fallback block seeding can skip table bytes.
    """
    data = exe.read_bytes()
    edges: dict[int, list[int]] = {}
    data_ranges: list[tuple[int, int]] = []
    for addr, text in insns:
        if "jmp DWORD PTR [" not in text:
            continue
        m = re.search(r"\[[^\]]*0x([0-9a-f]{5,})\]", text)
        if not m:
            continue
        base = int(m.group(1), 16)
        targets = decode_jump_table(data, base)
        if targets:
            edges[addr] = targets
            data_ranges.append((base, base + 4 * len(targets)))
    return edges, data_ranges


def build_blocks(insns: list[tuple[int, str]], entry_addrs: set[int]):
    """Split the linear objdump stream into basic blocks.

    Leaders are the first instruction, all known function/handler entries,
    direct branch targets, and the instruction after a branch/return.
    """
    addr_to_idx = {addr: i for i, (addr, _) in enumerate(insns)}
    leaders = {0}
    for addr in entry_addrs:
        if addr in addr_to_idx:
            leaders.add(addr_to_idx[addr])
    for i, (addr, text) in enumerate(insns):
        op = text.split(" ", 1)[0]
        if op in COND_BRANCHES or op in NO_FALLTHROUGH:
            if i + 1 < len(insns):
                leaders.add(i + 1)
            target = direct_target(text)
            if target is not None and target in addr_to_idx:
                leaders.add(addr_to_idx[target])
    ordered = sorted(leaders)
    blocks = []
    for pos, start in enumerate(ordered):
        end = (ordered[pos + 1] - 1) if pos + 1 < len(ordered) else len(insns) - 1
        blocks.append((start, end))
    addr_to_block = {insns[start][0]: bi for bi, (start, _) in enumerate(blocks)}
    return blocks, addr_to_block, addr_to_idx


def merge_states(a: dict[str, Provenance] | None,
                 b: dict[str, Provenance] | None) -> dict[str, Provenance]:
    if a is None:
        return dict(b or {})
    if b is None:
        return dict(a)
    out: dict[str, Provenance] = {}
    for reg in set(a) | set(b):
        out[reg] = widen_provenance(
            set(a.get(reg, frozenset())) | set(b.get(reg, frozenset()))
        )
    return out


def scan(insns: list[tuple[int, str]], methods: list[tuple[int, str]],
         reset_methods: list[tuple[int, str]] | None = None,
         handler_addrs: set[int] | None = None,
         indirect_edges: dict[int, list[int]] | None = None,
         data_ranges: list[tuple[int, int]] | None = None):
    """Return (accesses, base_loads, unresolved).

    This is a basic-block dataflow pass.  It follows direct branches, decoded
    indirect switch tables, and merges pointer provenance at joins.  Indirect
    jumps without a recovered table have no successor edges; the floor-handler
    entries are additionally seeded with their known live-in ``ebx``/``esi``
    pointer bases.
    """
    if reset_methods is None:
        reset_methods = methods
    if handler_addrs is None:
        handler_addrs = set()
    if indirect_edges is None:
        indirect_edges = {}
    data_ranges = list(data_ranges or []) + KNOWN_DATA_RANGES

    accesses: list[dict[str, object]] = []
    base_loads: list[dict[str, object]] = []
    unresolved: list[dict[str, object]] = []
    access_seen: set[tuple[int, str, str]] = set()
    base_seen: set[tuple[int, str, int]] = set()
    unresolved_seen: set[tuple[int, str]] = set()

    indirect_targets = {t for targets in indirect_edges.values() for t in targets}
    branch_targets = set(indirect_targets)
    for _, text in insns:
        op = text.split(" ", 1)[0]
        if op in COND_BRANCHES or op == "jmp":
            target = direct_target(text)
            if target is not None:
                branch_targets.add(target)
    entry_addrs = ({addr for addr, _ in reset_methods} | handler_addrs
                   | indirect_targets)
    blocks, addr_to_block, _ = build_blocks(insns, entry_addrs)

    def block_is_data(block_index: int) -> bool:
        addr = insns[blocks[block_index][0]][0]
        return any(lo <= addr < hi for lo, hi in data_ranges)

    def record_block(block_index: int, regs: dict[str, Provenance]) -> None:
        start, end = blocks[block_index]
        for idx in range(start, end + 1):
            addr, text = insns[idx]
            method = nearest_method(methods, addr)
            op, operands = split_operands(text)
            dest, src = dest_src(text)

            # Record base-address loads.
            if op == "mov" and dest in CANON and src:
                if re.match(r"^0x[0-9a-f]+$", src):
                    value = int(src, 16)
                    if value in BUFFER_ADDRS:
                        key = (addr, CANON[dest], value)
                        if key not in base_seen:
                            base_seen.add(key)
                            base_loads.append({
                                "address": addr, "method": method,
                                "instruction": text,
                                "register": CANON[dest], "base": value,
                            })
            if op == "lea" and dest in CANON and src and "[" in src:
                reg, disp, has_index = memory_base_disp(src)
                prov = regs.get(reg, frozenset()) if reg else frozenset()
                if not has_index and disp is not None:
                    for origin, offset, _fallback in prov:
                        if offset is None:
                            continue
                        value = origin + offset + disp
                        if value in BUFFER_ADDRS:
                            key = (addr, CANON[dest], value)
                            if key not in base_seen:
                                base_seen.add(key)
                                base_loads.append({
                                    "address": addr, "method": method,
                                    "instruction": text,
                                    "register": CANON[dest], "base": value,
                                })

            # Direct absolute references.
            literals = [int(x, 16) for x in re.findall(r"0x([0-9a-f]+)", text)]
            base_load_insn = (
                op == "mov" and dest in CANON and src is not None
                and bool(re.match(r"^0x[0-9a-f]+$", src))
                and int(src, 16) in BUFFER_ADDRS
            )
            direct_hit = False
            for value in literals:
                if value in BUFFER_ADDRS:
                    direct_hit = True
                    if base_load_insn:
                        continue
                    target, desc = TARGETS[value]
                    access = "read"
                    if is_memory(dest) and dest and f"0x{value:x}" in dest.lower():
                        access = "write"
                    if op in ("inc", "dec", "add", "sub", "or", "and") and is_memory(dest):
                        access = "read_write"
                    key = (addr, access, target)
                    if key not in access_seen:
                        access_seen.add(key)
                        accesses.append({
                            "address": addr, "method": method,
                            "instruction": text, "access": access,
                            "target": target, "target_desc": desc,
                            "mechanism": "direct", "confidence": "high",
                        })

            # Register-based references.  A register may hold multiple
            # possible buffer-derived pointers after a join; resolve every
            # known target and report unknown possibilities.
            if not direct_hit and op != "lea":
                for operand in (dest, src):
                    if not is_memory(operand):
                        continue
                    reg, disp, has_index = memory_base_disp(operand)
                    if reg is None:
                        continue
                    prov = regs.get(reg, frozenset())
                    if not prov:
                        continue
                    resolved: dict[str, tuple[str, list[bool]]] = {}
                    for origin, offset, fallback in prov:
                        if offset is None or disp is None:
                            reason = (f"pointer provenance from 0x{origin:08x} "
                                      "has unknown offset")
                            key = (addr, reason)
                            if key not in unresolved_seen:
                                unresolved_seen.add(key)
                                unresolved.append({
                                    "address": addr, "method": method,
                                    "instruction": text, "register": reg,
                                    "base": origin, "reason": reason,
                                })
                            continue
                        value = origin + offset + disp
                        target = target_for(value)
                        if target is None:
                            reason = f"possible base+disp 0x{value:x} is not a known field"
                            key = (addr, reason)
                            if key not in unresolved_seen:
                                unresolved_seen.add(key)
                                unresolved.append({
                                    "address": addr, "method": method,
                                    "instruction": text, "register": reg,
                                    "base": origin, "reason": reason,
                                })
                            continue
                        resolved.setdefault(target[0], (target[1], []))[1].append(fallback)
                    for target_name, (target_desc, fallbacks) in resolved.items():
                        access = "read"
                        if operand is dest:
                            access = ("read_write"
                                      if op in ("inc", "dec", "add", "sub", "or", "and")
                                      else "write")
                        key = (addr, access, target_name)
                        if key in access_seen:
                            continue
                        access_seen.add(key)
                        confidence = "high" if (not has_index or "*2" in operand) else "medium"
                        mechanism = "indexed" if has_index else "register"
                        if all(fallbacks):
                            confidence = "medium"
                            mechanism += "+fallback"
                        accesses.append({
                            "address": addr, "method": method,
                            "instruction": text, "access": access,
                            "target": target_name,
                            "target_desc": target_desc,
                            "mechanism": mechanism,
                            "confidence": confidence,
                        })

            apply_instruction(text, regs)

    in_states: list[dict[str, Provenance] | None] = [None] * len(blocks)
    for i, (start, _) in enumerate(blocks):
        if insns[start][0] in {addr for addr, _ in reset_methods}:
            in_states[i] = {}
    for addr in handler_addrs:
        block = addr_to_block.get(addr)
        if block is not None:
            # ichicheck leaves the count in ebx and the command buffer in esi
            # before jumping to the floor handler.
            in_states[block] = {
                "ebx": frozenset({(COUNT_ADDR, 0, False)}),
                "esi": frozenset({(CMD_BASE, 0, False)}),
            }
    in_states[0] = in_states[0] or {}

    worklist = collections.deque(i for i, s in enumerate(in_states) if s is not None)
    out_states: dict[int, dict[str, Provenance]] = {}

    def propagate(queue: collections.deque[int]) -> None:
        while queue:
            bi = queue.popleft()
            regs = dict(in_states[bi] or {})
            record_block(bi, regs)
            out_states[bi] = dict(regs)
            start, end = blocks[bi]
            last_text = insns[end][1]
            op = last_text.split(" ", 1)[0]
            successors: list[int] = []
            next_block = bi + 1 if bi + 1 < len(blocks) else None
            if op in COND_BRANCHES:
                target = direct_target(last_text)
                if target is not None and target in addr_to_block:
                    successors.append(addr_to_block[target])
                if next_block is not None:
                    successors.append(next_block)
            elif op == "jmp":
                target = direct_target(last_text)
                if target is not None and target in addr_to_block:
                    successors.append(addr_to_block[target])
                else:
                    for target in indirect_edges.get(insns[end][0], []):
                        block = addr_to_block.get(target)
                        if block is not None:
                            successors.append(block)
            elif op in ("ret", "retn", "retf", "iretd"):
                pass
            else:
                if next_block is not None:
                    successors.append(next_block)
            for succ in successors:
                if block_is_data(succ):
                    continue
                old = in_states[succ]
                new = merge_states(old, regs)
                if old is None or new != old:
                    in_states[succ] = new
                    queue.append(succ)

    propagate(worklist)

    # Fallback for method bodies whose entry block establishes a buffer base
    # but whose internal switch makes some case blocks unreachable in the
    # recovered CFG.  Seed those blocks with the entry state, skipping decoded
    # jump-table data ranges.  Such accesses are marked confidence=medium and
    # mechanism=...+fallback in the output.
    method_starts = sorted({addr for addr, _ in reset_methods})
    for idx, start in enumerate(method_starts):
        stop = method_starts[idx + 1] if idx + 1 < len(method_starts) else 0x488E94
        entry_block = addr_to_block.get(start)
        if entry_block is None:
            continue
        entry_out = out_states.get(entry_block) or in_states[entry_block] or {}
        if not any(prov for prov in entry_out.values()):
            continue
        fallback_state = {
            reg: frozenset((origin, offset, True)
                           for origin, offset, _fallback in prov)
            for reg, prov in entry_out.items()
            if prov
        }
        seeded: list[int] = []
        for bi, (block_start, _) in enumerate(blocks):
            addr = insns[block_start][0]
            if not (start <= addr < stop) or in_states[bi] is not None:
                continue
            if addr not in branch_targets:
                continue
            if any(lo <= addr < hi for lo, hi in data_ranges):
                continue
            in_states[bi] = dict(fallback_state)
            seeded.append(bi)
        if seeded:
            propagate(collections.deque(seeded))

    # Process blocks with no recovered predecessor once, with an empty state,
    # so direct absolute accesses are still inventoried.  This is how
    # jump-table data and uncalled blocks are scanned without inheriting a
    # bogus pointer.
    for i, state in enumerate(in_states):
        if state is None:
            record_block(i, {})

    return accesses, base_loads, unresolved


def summarize(accesses, base_loads, unresolved) -> str:
    by_target = collections.Counter(str(a["target"]) for a in accesses)
    by_access = collections.Counter(str(a["access"]) for a in accesses)
    by_method = collections.Counter(str(a["method"]) for a in accesses)
    by_mechanism = collections.Counter(str(a["mechanism"]) for a in accesses)
    lines = [
        f"accesses: {len(accesses)}",
        f"base loads: {len(base_loads)}",
        f"unresolved candidates: {len(unresolved)}",
        "by target: " + ", ".join(f"{k}={v}" for k, v in sorted(by_target.items())),
        "by access: " + ", ".join(f"{k}={v}" for k, v in sorted(by_access.items())),
        "by mechanism: " + ", ".join(f"{k}={v}" for k, v in sorted(by_mechanism.items())),
        "methods with accesses:",
    ]
    for method, count in by_method.most_common():
        lines.append(f"  {method}: {count}")
    if unresolved:
        lines.append("unresolved:")
        for item in unresolved[:25]:
            lines.append(
                f"  {item['address']:08x} {item['method']} {item['instruction']} "
                f"({item['reason']})"
            )
        if len(unresolved) > 25:
            lines.append(f"  ... {len(unresolved) - 25} more")
    return "\n".join(lines)


def write_inventory(path: Path, exe_name: str, digest: str, accesses,
                    base_loads, unresolved) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        f.write(f"# event buffer inventory for {exe_name}\n")
        f.write(f"# sha256 {digest}\n")
        f.write("# address\tmethod\taccess\ttarget\tmechanism\tconfidence\tinstruction\n")
        for a in sorted(accesses, key=lambda x: (int(x["address"]), str(x["target"]))):
            f.write(
                f"{int(a['address']):08x}\t{a['method']}\t{a['access']}\t"
                f"{a['target']}\t{a['mechanism']}\t{a['confidence']}\t"
                f"{a['instruction']}\n"
            )
        f.write("\n# base-address loads\n")
        for b in sorted(base_loads, key=lambda x: int(x["address"])):
            f.write(
                f"{int(b['address']):08x}\t{b['method']}\tload\t"
                f"0x{int(b['base']):08x}\tregister\t-\t{b['instruction']}\n"
            )
        f.write("\n# unresolved candidates\n")
        for u in sorted(unresolved, key=lambda x: int(x["address"])):
            f.write(
                f"{int(u['address']):08x}\t{u['method']}\tunresolved\t{u['reason']}\t"
                f"register\tlow\t{u['instruction']}\n"
            )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("exe", type=Path)
    ap.add_argument("-o", "--output", type=Path)
    ap.add_argument("--summary", action="store_true")
    ap.add_argument("--allow-other", action="store_true",
                    help="scan an unpinned binary (diagnostic only)")
    args = ap.parse_args()

    if not args.exe.exists():
        print(f"error: {args.exe} does not exist", file=sys.stderr)
        return 2

    digest = sha256(args.exe)
    if digest != EXPECTED_SHA256:
        if not args.allow_other:
            print(
                f"error: binary hash {digest} does not match the pinned "
                f"{EXPECTED_SHA256}; use --allow-other only for diagnostics",
                file=sys.stderr,
            )
            return 3
        print("warning: scanning an unpinned binary; addresses may be wrong",
              file=sys.stderr)

    cache = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "tsw"
    output = args.output or cache / "event_writers.tsv"
    true_methods = load_methods(Path(__file__).resolve().parents[1] / "method-table.tsv")
    methods = sorted(true_methods + EVENT_HANDLERS)
    handler_addrs = {addr for addr, _ in EVENT_HANDLERS}
    entry_addrs = {addr for addr, _ in true_methods} | handler_addrs
    insns = disassemble(args.exe, entry_addrs)
    indirect_edges, data_ranges = recover_indirect_edges(args.exe, insns)
    # Jump-table targets must also be instruction boundaries; resynchronise
    # the disassembly at them before the dataflow pass.
    entry_addrs |= {t for targets in indirect_edges.values() for t in targets}
    insns = disassemble(args.exe, entry_addrs)
    indirect_edges, data_ranges = recover_indirect_edges(args.exe, insns)
    accesses, base_loads, unresolved = scan(insns, methods, true_methods,
                                            handler_addrs, indirect_edges,
                                            data_ranges)

    if args.summary:
        print(f"binary: {args.exe}")
        print(f"sha256: {digest}")
        print(summarize(accesses, base_loads, unresolved))
        return 0

    write_inventory(output, args.exe.name, digest, accesses, base_loads, unresolved)
    print(f"wrote {output}")
    print(summarize(accesses, base_loads, unresolved))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
