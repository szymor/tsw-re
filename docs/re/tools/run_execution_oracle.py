#!/usr/bin/env python3
"""P3b execution oracle: builder + original ``stackwork`` drain.

The execution oracle (16-event-pilot.md section 8).  For one golden-vector witness
(export_event_vectors.WITNESSES) this tool

1. runs the original builder through the native harness (Mode A, identical
   inputs to the exported vector);
2. keeps calling the original ``stackwork`` (0x4526c0) - the routine
   ``timer2timer`` (0x45265c) calls up to twice per timer event - until the
   command count drains, a call fails closed, or ``--max-steps`` is reached;
3. records, per consumed command: the cursor, the command words, the
   auxiliaries the command sees, every write the step makes to the state
   block and the map block (instruction address, index, old/new value), the
   command count after the step, and the typed presentation stubs it called.

Writes to the state and map are hooked during the builder phase too, so
the output names the instruction that set every changed field.

Fail closed: a step that reaches an unclassified call/transfer stops the
drain; the report gives the command, the transfer and the target.  A
stopped drain is evidence, not an error: it marks where presentation code
(VCL windows, zyouout, media) has no contract yet.

Raw output is generated data (it embeds the user's map/state snapshot) and
stays outside the repository.

Opcode 0x0a disables Timer2 (0x453194: TTimer.SetEnabled(form+0x2ec, False))
and opens the zyouout window, so the drain stops after it ("suspended").
``--complete-dialogs`` models the player confirming every dialogue window
(any ``zyouout`` call except the g_subevent_id 9 path): the original
``Button1Click`` (0x44da10), which dispatches on g_subevent_id (0x48c5a4)
and may end in timer2on, is called and the timeline is drained again.

``--permissive-vcl`` (drain phase only) returns 0 from unclassified Delphi
RTL/VCL calls instead of stopping, records each one, and marks the report
``tainted``.  Game code (native_event_oracle.ALLOWED_CALL_RANGES) still runs
natively and every state/map write is still hooked, so a tainted drain is
evidence of *where* state changes happen, not of presentation parity.

Usage:
    python3 docs/re/tools/run_execution_oracle.py --list
    python3 docs/re/tools/run_execution_oracle.py --case handan_floor3 -o /tmp/drain.json
    python3 docs/re/tools/run_execution_oracle.py --all -d ~/.cache/tsw/drain --summary

Unicorn must be importable (for example the venv used by run_native_oracle).
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import export_event_vectors as vectors  # noqa: E402
import native_event_oracle as native  # noqa: E402

try:
    from unicorn import UC_HOOK_MEM_WRITE
except ImportError:  # pragma: no cover - reported by NativeHarness
    UC_HOOK_MEM_WRITE = None

STACKWORK = 0x4526C0
BUTTON1CLICK = 0x44DA10       # dialogue OK: dispatches on g_subevent_id
SUBEVENT_ADDR = 0x48C5A4      # g_subevent_id (aux "subevent_id")
ZYOUOUT = 0x449E18            # dialogue window
SET_VISIBLE = 0x413500        # TControl.SetVisible(self=eax, value=dl)
BUTTON1_FIELD = 0x1D0         # form field: Button1 (OK)
BUTTON2_FIELD = 0x1D4         # form field: Button2 (Yes); Button7 (No) +0x1d8
TIMER2_FIELD = 0x2EC          # form field: Timer2 (the timeline timer)
TIMER_SET_ENABLED = 0x42C454  # TTimer.SetEnabled(self=eax, value=dl)
SHOW_MODAL = 0x4237DC         # TCustomForm.ShowModal (the ranking form)
MESSAGE_DLG = 0x42CEC8        # MessageDlg (the death prompt)
BUTTON_OBJECTS = {BUTTON1_FIELD: 0x10010000, BUTTON2_FIELD: 0x10011000,
                  TIMER2_FIELD: 0x10012000}
TIMER2TIMER = 0x45265C
STATE_SIZE = 684
MAP_SIZE = 6273
STEP_LIMIT = 200_000          # per stackwork call
# Stack bytes popped by permissively stubbed callees that take stack
# arguments (Delphi register convention passes the 4th+ argument on the
# stack and the callee ends in `ret N`).  Checked in Ghidra.
PERMISSIVE_STACK_BYTES = {
    0x41A530: 4,   # TCanvas.CopyRect(dest, canvas, source): ret 4
    0x417E44: 8,   # 5-argument register-convention VCL helper: ret 8
}
# Game code executed only by the VM, outside the P3 builder ranges.  Added to
# native_event_oracle.ALLOWED_CALL_RANGES for the drain phase only.
DRAIN_CALL_RANGES = [
    (0x483000, 0x484740),  # openning9 (0x482e60) switch cases up to 0x48473e
]
PERMISSIVE_KINDS = {"unsupported_call", "unsupported_indirect_call",
                    "unclassified_vtable_slot"}


class ExecutionHarness(native.NativeHarness):
    """NativeHarness that can keep running ``stackwork`` after the builder."""

    def __init__(self, exe: Path, permissive_vcl: bool = False):
        super().__init__(exe, presentation_stubs=True)
        self.phase = "builder"
        self.permissive_vcl = permissive_vcl
        self.permissive_stubs: list[dict[str, object]] = []
        self.writes: list[dict[str, object]] = []
        self.window_open = False
        self.choice_open = False
        self.timer2 = False
        self.drain_segments: list[list[list[int]]] = []
        self._add_button_objects()
        self._add_write_hooks()
        self._add_window_hook()

    def _add_button_objects(self) -> None:
        """Give Button1 and Button2 their own dummy objects (same layout as
        the shared one) so SetVisible on them can be told apart."""
        size = native.OBJECT_SIZE
        for field, base in BUTTON_OBJECTS.items():
            self.uc.mem_map(base, size, 7)
            self.uc.mem_write(base, base.to_bytes(4, "little") * (size // 4))
            self.uc.mem_write(base, native.VTABLE_BASE.to_bytes(4, "little"))
            self.uc.mem_write(base + 0x2C, (32).to_bytes(4, "little"))
            self.uc.mem_write(base + 0x30, (32).to_bytes(4, "little"))
            self.uc.mem_write(native.FORM_BASE + field, base.to_bytes(4, "little"))

    def _add_window_hook(self) -> None:
        """zyouout (0x449e18) shows the dialogue window and its OK button
        unless g_subevent_id == 9 (game-over path, no window)."""
        def on_zyouout(uc, address, size, user_data):
            if self.u32(SUBEVENT_ADDR) != 9:
                self.window_open = True

        self.uc.hook_add(native.UC_HOOK_CODE, on_zyouout,
                         begin=ZYOUOUT, end=ZYOUOUT)

    def _record_stub(self, target, contract, caller, kind):
        # SetVisible is a presentation stub (intercepted at the call site).
        # Handlers that show Button1 themselves (syounin's thank-you text,
        # Button2Click's result) wait for the same OK as zyouout; Button2
        # (Yes) shown means a merchant/altar question awaits an answer.
        if (target == TIMER_SET_ENABLED and self.uc.reg_read(native.UC_X86_REG_EAX)
                == BUTTON_OBJECTS[TIMER2_FIELD]):
            self.timer2 = bool(self.uc.reg_read(native.UC_X86_REG_EDX) & 0xFF)
        if target == MESSAGE_DLG:
            # zyouout's death prompt replaces the dialogue window: no OK
            # button to confirm; Yes runs timer2on, No enables Game>Load.
            self.window_open = False
        if target == SHOW_MODAL:
            # The ranking form's OnClose (form2onclose 0x43a504) enables
            # TTSW10's Timer2 before ShowModal returns.
            self.timer2 = True
        if target == SET_VISIBLE:
            obj = self.uc.reg_read(native.UC_X86_REG_EAX)
            shown = bool(self.uc.reg_read(native.UC_X86_REG_EDX) & 0xFF)
            if obj == BUTTON_OBJECTS[BUTTON1_FIELD]:
                self.window_open = shown
            elif obj == BUTTON_OBJECTS[BUTTON2_FIELD]:
                self.choice_open = shown
        return super()._record_stub(target, contract, caller, kind)

    def _insn_len(self, address: int) -> int:
        """Length of an unprefixed E8 / FF /2 call at address (0 if not)."""
        try:
            code = bytes(self.uc.mem_read(address, 8))
        except Exception:
            return 0
        if code[0] == 0xE8:
            return 5
        if code[0] != 0xFF or ((code[1] >> 3) & 7) != 2:
            return 0
        modrm = code[1]
        mod, rm = modrm >> 6, modrm & 7
        n = 2
        if mod != 3 and rm == 4:
            sib = code[2]
            n += 1
            if mod == 0 and (sib & 7) == 5:
                n += 4
        if mod == 1:
            n += 1
        elif mod == 2 or (mod == 0 and rm == 5):
            n += 4
        return n

    def _unsupported(self, kind: str, target: int, caller: int) -> None:
        # Drain-only permissive mode: Delphi RTL/VCL calls (outside the game
        # code ranges) return 0 and are recorded; the report is tainted.
        if (self.phase == "drain" and self.permissive_vcl
                and kind in PERMISSIVE_KINDS
                and not native.is_allowed_call(target)):
            n = self._insn_len(caller)
            if n:
                self.permissive_stubs.append(
                    {"kind": kind, "target": target, "caller": caller})
                pop = PERMISSIVE_STACK_BYTES.get(target, 0)
                if pop:
                    esp = int(self.uc.reg_read(native.UC_X86_REG_ESP))
                    self.uc.reg_write(native.UC_X86_REG_ESP, esp + pop)
                self.uc.reg_write(native.UC_X86_REG_EAX, 0)
                self.uc.reg_write(native.UC_X86_REG_EIP, caller + n)
                return None
        return super()._unsupported(kind, target, caller)

    def _add_write_hooks(self) -> None:
        uc = self.uc

        def on_write(uc, access, address, size, value, user_data):
            eip = int(uc.reg_read(native.UC_X86_REG_EIP))
            if native.STATE_BASE <= address < native.STATE_BASE + STATE_SIZE:
                region, base = "state", native.STATE_BASE
            else:
                region, base = "map", native.MAP_BASE
            off = address - base
            old = bytes(uc.mem_read(address, size))
            self.writes.append({
                "phase": self.phase, "region": region, "eip": eip,
                "offset": off,
                "index": off // 4 if region == "state" else off,
                "size": size,
                "old": int.from_bytes(old, "little"),
                "new": value & ((1 << (8 * size)) - 1),
            })

        uc.hook_add(UC_HOOK_MEM_WRITE, on_write,
                    begin=native.STATE_BASE,
                    end=native.STATE_BASE + STATE_SIZE - 1)
        uc.hook_add(UC_HOOK_MEM_WRITE, on_write,
                    begin=native.MAP_BASE, end=native.MAP_BASE + MAP_SIZE - 1)

    # -- low-level access --------------------------------------------------

    def u32(self, addr: int) -> int:
        return struct.unpack("<I", bytes(self.uc.mem_read(addr, 4)))[0]

    def count(self) -> int:
        # g_tl_count is a signed int: op 0x16 -> gameover -> syokidata2 sets
        # it to 0 and stackwork's common tail then leaves -1.
        v = self.u32(native.COUNT_ADDR)
        return v - (1 << 32) if v & 0x80000000 else v

    def command(self, index: int) -> list[int]:
        raw = bytes(self.uc.mem_read(native.CMD_BASE + index * 6, 6))
        return list(struct.unpack("<HHH", raw))

    def aux(self) -> dict[str, int]:
        return {name: self.u32(addr) for name, addr in native.AUX_ADDRS.items()}

    # -- one stackwork invocation -------------------------------------------

    def run_stackwork(self) -> tuple[str, str, list[dict[str, object]]]:
        return self.run_entry(STACKWORK)

    def run_entry(self, entry: int) -> tuple[str, str, list[dict[str, object]]]:
        mark = len(self.executed_runs)
        try:
            return self._run_entry(entry)
        finally:
            # Coverage: each stackwork / Button1Click call is its own trace.
            self.drain_segments.append(
                [list(run) for run in self.executed_runs[mark:]])

    def _run_entry(self, entry: int) -> tuple[str, str, list[dict[str, object]]]:
        uc = self.uc
        self._reset_stack()
        self.steps = 0
        self.error = ""
        self.invalid_addr = ""
        self.termination = ""
        self.unsupported_transfer = None
        before = len(self.stub_invocations)
        uc.reg_write(native.UC_X86_REG_EAX, native.FORM_BASE)
        for reg in (native.UC_X86_REG_EBX, native.UC_X86_REG_ECX,
                    native.UC_X86_REG_EDX, native.UC_X86_REG_ESI,
                    native.UC_X86_REG_EDI, native.UC_X86_REG_EBP):
            uc.reg_write(reg, 0)
        try:
            uc.emu_start(entry, native.RETURN_SENTINEL, count=STEP_LIMIT)
        except Exception as exc:  # unicorn fault
            eip = int(uc.reg_read(native.UC_X86_REG_EIP))
            self.termination = "fault"
            self.error = f"{exc} eip=0x{eip:08x} {self.invalid_addr}"
        eip = int(uc.reg_read(native.UC_X86_REG_EIP))
        if not self.termination:
            if eip == native.RETURN_SENTINEL:
                self.termination = "returned"
            elif self.steps >= STEP_LIMIT:
                self.termination = "step_limit"
            else:
                self.termination = "stopped_other"
        detail = self.error
        if self.unsupported_transfer:
            kind, target, caller = self.unsupported_transfer
            detail = f"{kind} target=0x{target:08x} caller=0x{caller:08x}"
        stubs = [{"name": s.get("name"), "caller": s.get("caller"),
                  "kind": s.get("kind"),
                  "edx": (s.get("registers") or {}).get("edx")}
                 for s in self.stub_invocations[before:]]
        return self.termination, detail, stubs


def run_case(exe: Path, w: vectors.Witness, max_steps: int,
             permissive_vcl: bool = False,
             complete_dialogs: bool = False) -> dict:
    snap = w.snapshot()
    state = list(snap.state)
    aux_in = snap.aux_with_derived()
    native.STUB_CONTRACTS[MESSAGE_DLG]["return"] = int(
        getattr(w, "message_dlg", 6))
    h = ExecutionHarness(exe, permissive_vcl)
    h.file_bytes = getattr(w, "file_bytes", lambda: b"")()
    builder = h.call(w.entry, state, snap.map_block, w.floor, aux_in,
                     commands=w.native_commands or None,
                     count=w.native_count)
    report: dict[str, object] = {
        "schema": "tsw-event-drain/1",
        "binary_sha256": builder.digest,
        "case_id": w.case_id,
        "handler": w.handler,
        "entry": f"0x{w.entry:08x}",
        "builder": {
            "ok": builder.ok,
            "termination": builder.termination,
            "error": builder.error,
            "commands": builder.command_count,
            "aux_final": builder.aux,
            "writes": [x for x in h.writes if x["phase"] == "builder"],
        },
    }
    steps: list[dict[str, object]] = []
    modal: list[dict[str, object]] = []
    stop = "drained"
    if not builder.ok:
        stop = "builder_failed"
    h.phase = "drain"
    saved_ranges = list(native.ALLOWED_CALL_RANGES)
    native.ALLOWED_CALL_RANGES.extend(DRAIN_CALL_RANGES)

    def drain() -> str:
        # timer2timer (0x45265c) makes two stackwork calls per timer event;
        # Timer2 restarts after each resume, so ticks are counted from here.
        tick_pos = 0
        suspend_after = ""
        while h.count() > 0:
            if len(steps) >= max_steps:
                return "max_steps"
            cursor = h.count()
            cmd = h.command(cursor)
            aux = h.aux()
            mark = len(h.writes)
            pmark = len(h.permissive_stubs)
            timer_before = h.timer2
            termination, detail, stubs = h.run_stackwork()
            entry = {
                "step": len(steps),
                "round": len(modal),
                "cursor": cursor,
                "command": cmd,
                "aux_before": aux,
                "termination": termination,
                "count_after": h.count(),
                "writes": h.writes[mark:],
                "stubs": stubs,
                "permissive_stubs": h.permissive_stubs[pmark:],
            }
            if detail:
                entry["detail"] = detail
            steps.append(entry)
            if termination != "returned":
                return f"step_failed:{termination}"
            first_of_tick = tick_pos % 2 == 0
            tick_pos += 1
            if suspend_after:
                return suspend_after
            if (cmd[0] != 0x0A and timer_before and not h.timer2
                    and not h.window_open):
                # Another handler stopped Timer2 (openning9 phase 8: the
                # ending's buttons).  The tick's second stackwork call
                # still runs; then no timer event comes.
                if first_of_tick and h.count() > 0:
                    suspend_after = "timer_off"
                    continue
                return "timer_off"
            if cmd[0] == 0x0A and h.timer2 and not h.window_open:
                # zyouout answered itself (the death prompt's Yes ran
                # timer2on): the timeline goes on.
                continue
            if cmd[0] == 0x0A and not h.window_open:
                # The death prompt's No: Timer2 stays off, nothing to confirm.
                return "timer_off"
            if cmd[0] == 0x0A:
                # 0x453194: op 0x0a disables Timer2 (form+0x2ec) and opens the
                # zyouout window; no further timer event comes until the
                # dialogue is confirmed (Button1Click -> timer2on).  The
                # second stackwork call of the current timer event still
                # runs when 0x0a was the first one.
                if first_of_tick and h.count() > 0:
                    suspend_after = "suspended"
                    continue
                return "suspended"
        return "drained"

    if builder.ok:
        stop = drain()
        # Modal completion: a 0x0a sub-event leaves g_subevent_id set and a
        # zyouout window open; the original continues in Button1Click when
        # the player confirms.  Model that confirmation and drain again.
        while (complete_dialogs and stop in ("drained", "suspended")
               and h.window_open and len(modal) < 32):
            sub = h.u32(SUBEVENT_ADDR)
            h.window_open = False
            aux = h.aux()
            mark = len(h.writes)
            pmark = len(h.permissive_stubs)
            termination, detail, stubs = h.run_entry(BUTTON1CLICK)
            m = {
                "round": len(modal) + 1,
                "subevent": sub,
                "entry": f"0x{BUTTON1CLICK:08x}",
                "aux_before": aux,
                "aux_after": h.aux(),
                "termination": termination,
                "count_after": h.count(),
                "commands": [h.command(i) for i in range(1, min(h.count(), 4096) + 1)],
                "writes": h.writes[mark:],
                "stubs": stubs,
                "permissive_stubs": h.permissive_stubs[pmark:],
            }
            if detail:
                m["detail"] = detail
            modal.append(m)
            if termination != "returned":
                stop = f"modal_failed:{termination}"
                break
            if h.count() == 0 and not h.window_open:
                stop = "modal_idle"
                break
            if h.count() > 0 and not h.timer2 and not h.window_open:
                # The confirm did not restart Timer2: the rest of the
                # timeline waits for the next timer2on.
                stop = "timer_off"
                break
            stop = drain()
        # A Yes/No question is not answered by the drain: the timeline stops
        # there, like the game waiting for the player.
        if stop in ("drained", "modal_idle") and h.choice_open and not h.window_open:
            stop = "choice_pending"
    native.ALLOWED_CALL_RANGES[:] = saved_ranges
    final_state = bytes(h.uc.mem_read(native.STATE_BASE, STATE_SIZE))
    final_map = bytes(h.uc.mem_read(native.MAP_BASE, MAP_SIZE))
    st = list(struct.unpack("<171i", final_state))
    report["drain"] = {
        "stop": stop,
        "permissive_vcl": permissive_vcl,
        "complete_dialogs": complete_dialogs,
        "tainted": bool(h.permissive_stubs),
        "permissive_targets": sorted({f"0x{p['target']:08x}"
                                      for p in h.permissive_stubs}),
        "steps": len(steps),
        "count_remaining": h.count(),
        "state_diff": [{"index": i, "before": a, "after": b}
                       for i, (a, b) in enumerate(zip(state, st)) if a != b],
        "map_diff": [{"index": i, "before": a, "after": b}
                     for i, (a, b) in enumerate(zip(snap.map_block, final_map))
                     if a != b],
        "aux_final": h.aux(),
        "trace": steps,
        "modal": modal,
        "executed_segments": h.drain_segments,
    }
    return report


def summarize(r: dict) -> str:
    d = r["drain"]
    lines = [f"{r['case_id']}: builder={r['builder']['commands']} cmds, "
             f"drain stop={d['stop']} steps={d['steps']} "
             f"remaining={d['count_remaining']}"
             f"{' tainted' if d['tainted'] else ''}"]
    for s in d["trace"]:
        if s["writes"] or s["termination"] != "returned":
            ws = ", ".join(
                f"{w['region']}[{w['index']:#x}] {w['old']}->{w['new']}"
                f"@{w['eip']:#x}" for w in s["writes"])
            lines.append(f"  step {s['step']} cursor {s['cursor']} "
                         f"cmd {s['command']} -> {s['termination']}"
                         f"{' ' + s.get('detail', '') if s.get('detail') else ''}"
                         f"{' writes: ' + ws if ws else ''}")
    for m in d.get("modal", []):
        ws = ", ".join(
            f"{w['region']}[{w['index']:#x}] {w['old']}->{w['new']}"
            f"@{w['eip']:#x}" for w in m["writes"])
        lines.append(f"  modal round {m['round']} subevent {m['subevent']} "
                     f"-> {m['termination']} {m.get('detail', '')} "
                     f"count {m['count_after']}"
                     f"{' writes: ' + ws if ws else ''}")
    for w in r["builder"]["writes"]:
        lines.append(f"  builder write {w['region']}[{w['index']:#x}] "
                     f"{w['old']}->{w['new']} @{w['eip']:#x}")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--exe", type=Path, default=vectors.DEFAULT_EXE)
    ap.add_argument("--case", action="append", default=[])
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--max-steps", type=int, default=2000)
    ap.add_argument("-o", "--output", type=Path)
    ap.add_argument("-d", "--output-dir", type=Path)
    ap.add_argument("--summary", action="store_true")
    ap.add_argument("--permissive-vcl", action="store_true",
                    help="drain only: stub unclassified RTL/VCL calls (eax=0) "
                         "and mark the report tainted")
    ap.add_argument("--complete-dialogs", action="store_true",
                    help="after each drain, confirm a pending 0x0a sub-event "
                         "window by calling the original Button1Click and "
                         "drain again")
    args = ap.parse_args()
    by_id = {w.case_id: w for w in vectors.WITNESSES}
    if args.list:
        for w in vectors.WITNESSES:
            print(f"{w.case_id:24} {w.handler:12} {w.note}")
        return 0
    names = list(by_id) if args.all else args.case
    if not names:
        ap.error("give --case NAME, --all or --list")
    for name in names:
        if name not in by_id:
            ap.error(f"unknown case {name}")
    if args.output and len(names) != 1:
        ap.error("-o needs exactly one case")
    for name in names:
        report = run_case(args.exe, by_id[name], args.max_steps,
                          args.permissive_vcl, args.complete_dialogs)
        if args.summary or not (args.output or args.output_dir):
            print(summarize(report))
        text = json.dumps(report, indent=1, sort_keys=True) + "\n"
        if args.output:
            args.output.write_text(text)
        if args.output_dir:
            args.output_dir.mkdir(parents=True, exist_ok=True)
            (args.output_dir / f"{name}.json").write_text(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
