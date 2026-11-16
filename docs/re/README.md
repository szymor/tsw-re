# Tower of the Sorcerer v1.2 — reverse-engineering notes

This directory documents the reverse engineering of **Tower of the Sorcerer
v1.2** (`TSW.exe`), a Delphi 2.0 32-bit Windows game.  The goal is a faithful
C + SDL3 re-implementation of the gameplay, using only user-owned original
assets at runtime or placeholder/procedural art.

## Scope and status

| Area | Status |
|---|---|
| Binary / PE / Delphi forms | done |
| Method table and function map | done |
| Map format (51 floors, 11×11) | done |
| Initial map extraction | done (embedding in `syokidata` verified) |
| State block / save file format | done |
| Checksums / save-slot logic | done |
| Input, movement, doors, keys | done |
| Item/equipment pickups and effects | done |
| Monster table and battle formulas | done (core formulas) |
| NPC / boss / floor-event scripts | in progress (oracle-driven; see `16-event-pilot.md` §9) |
| Text/message table | mapped (`18-messages.md`); raw extraction stays outside the repo |
| UI rendering details | layout and controls known; font quirk documented |
| Re-implementation | **Phases 0–3.5 done; Phase 4 (floors 11–50) in progress** (`08-implementation-plan.md`) |

## What is in this directory

* `01-binary-and-assets.md` — executable, Delphi forms, assets, tooling.
* `02-map-format.md` — save map block, runtime layout, tile values.
* `03-state-and-save.md` — 684-byte state block, save/load, checksums.
* `04-game-logic.md` — input, movement, doors, stairs, items, battle.
* `05-ui-and-text.md` — screen layout, menus, text/font issue.
* `06-data-tables.md` — monster table, item and key mappings.
* `07-method-table.md` — full published method table (name → address).
* `08-implementation-plan.md` — architecture, phases, ported events and what is left.
* `09-open-questions.md` — unresolved details and how to resolve them.
* `10-events-and-items.md` — per-floor event dispatcher, timeline format,
  special-item slots and equipment placements.
* `11-event-analysis-plan.md` — trace-free plan for fully recovering the
  event VM, trigger graph, timelines and flag map.
* `12-event-vm.md` — structural reference for the timeline VM, opcodes and
  sub-event cursor semantics.
* `13-event-triggers.md` — decoded trigger switch tables, input/NPC/mevent
  call graph and first-pass trigger inventory.
* `14-event-oracle-contract.md` — entry-point ABI, deterministic snapshot,
  stub/filesystem/determinism contract for the event oracle.
* `15-event-flags.md` — event flag map and state-index corrections.
* `16-event-pilot.md` — pilot lifts (floor 10, `roujin`, opening), P3b
  execution-oracle results, the coverage matrix and the ported handler
  batches.
* `17-static-findings.md` — static findings from the Ghidra annotation pass:
  `openning31` demotion, choice-confirm flag writes, `open0.bmp` intro,
  full raw→monster mapping, `taisen2` forecast, timeline builders.
* `18-messages.md` — dialogue text: `ListBox2` and the `zyouout` index rules.
* `19-windows.md` — the dialogue windows' run-time layout (`form_layout.py`).
* `schemas/` — header-only schemas for the generated event tables.
* [`../design/00-design.md`](../design/00-design.md) — PoC design: 640×480
  layout, asset pipeline, text policy, architecture and milestones.
* `tools/` — scripts that read the user's own game files and produce local
  data; the generated assets are not committed.

## Legal / asset policy

Project decisions:

* Converted sprites, maps, music and other generated assets are **not**
  committed.  The final build includes them; the conversion scripts are in
  the repository.
* Raw extracted game text is generated **outside the repository**, by default
  into `$XDG_CACHE_HOME/tsw/` (or `~/.cache/tsw/`), and is **not** committed.
  Refined text may be committed under `text/` later, once the message format
  is stable; distribution rights remain the project owner's responsibility.
* The monster stat table is included as small factual interoperability data;
  see `06-data-tables.md`.
* Extraction scripts read files from the user's own installation.  Do not
  redistribute generated asset data unless you have the right to do so.

## Reproduction environment

* Game: `~/.wine/drive_c/Program Files (x86)/Tower of the Sorcerer/TSW.exe`
* Runtime: Wine 11.21 inside a nested KWin/Xwayland session on `DISPLAY=:2`.
* Static analysis: Ghidra (MCP server) plus `objdump -d -M intel`.
* Runtime analysis: full process memory dumps through `/proc/<pid>/mem`.
* Saves/rankings: `Savedat/` directory.

The addresses in these notes are **runtime virtual addresses** (image base
`0x400000`).  File offsets are distinguished explicitly where used.
