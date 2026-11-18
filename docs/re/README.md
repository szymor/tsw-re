# Tower of the Sorcerer v1.2 — reverse-engineering notes

This directory documents the reverse engineering of **Tower of the Sorcerer
v1.2** (`TSW.exe`), a Delphi 2.0 32-bit Windows game, and how the remake
(*Tower of the Sorcerer: Revisited*) maps onto it.  Everything the original
does that the game needs is recovered and ported; the few details still
open are in `09-open-questions.md`.

## What is in this directory

The original game:

* `01-binary-and-assets.md` — executable, Delphi forms, assets, tooling.
* `02-map-format.md` — save map block, runtime layout, tile values.
* `03-state-and-save.md` — the 684-byte state block, saves, options,
  rankings.
* `04-game-logic.md` — input, movement, doors, stairs, items, battle.
* `05-ui-and-text.md` — screen layout, menus, text.
* `06-data-tables.md` — monster table, item and key mappings.
* `07-method-table.md` — the published method table (name → address).
* `10-events-and-items.md` — the per-floor event dispatcher, event
  placements, special items and their effects.
* `12-event-vm.md` — the timeline VM: opcodes, cursor, Timer2, dialogue
  suspension, and how the remake runs it (§13).
* `13-event-triggers.md` — trigger switch tables and the event call graph.
* `15-event-flags.md` — event flags and state indices.
* `17-static-findings.md` — findings from the Ghidra annotation pass.
* `18-messages.md` — dialogue text: `ListBox2` and its index rules.
* `19-windows.md` — the windows' run-time layout (`form_layout.py`).
* `20-audio.md` — sounds, tunes and their switches.

The remake and how it is checked:

* `08-remake.md` — the remake: source layout, how it presents the
  original, testing, decisions.
* `14-event-oracle-contract.md` — how the original's code is run on
  synthetic state (the oracle).
* `16-event-pilot.md` — the method, the execution oracle, the coverage
  gate and notes per ported handler.
* `09-open-questions.md` — what is still open.
* `schemas/` — header-only schemas for the analysis tables.
* `tools/` — the analysis tools and the oracle (`tools/README.md`).

## Legal / asset policy

Project decisions:

* The converted sprites, maps, sounds, music and the game text are
  committed under `data/` and ship with the build (decided 2026-11-17: NAO
  allows fan works based on the game, and a build then needs no original
  files).  The conversion scripts that made them are in the repository.
* The text in `data/text/<lang>/` is the source to edit and translate; the
  extraction scripts write to `$XDG_CACHE_HOME/tsw/` (or `~/.cache/tsw/`),
  for comparison with the original.
* The monster stat table is included as small factual interoperability data;
  see `06-data-tables.md`.
* Licences: the program (code, builders, tools, these notes) is
  GPL-3.0-or-later; the original game's material in `data/` and its
  translations are not, and are used with NAO's permission.  The README's
  *Licence* section lists which file is which.

## Reproduction environment

* Game: `~/.wine/drive_c/Program Files (x86)/Tower of the Sorcerer/TSW.exe`
* Runtime: Wine 11.21 inside a nested KWin/Xwayland session on `DISPLAY=:2`.
* Static analysis: Ghidra (MCP server) plus `objdump -d -M intel`.
* Runtime analysis: full process memory dumps through `/proc/<pid>/mem`.
* Saves/rankings: `Savedat/` directory.

The addresses in these notes are **runtime virtual addresses** (image base
`0x400000`).  File offsets are distinguished explicitly where used.
