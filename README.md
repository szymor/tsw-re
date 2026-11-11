# tsw-re

Reverse-engineering notes and a planned C + SDL3 re-implementation of
**Tower of the Sorcerer v1.2**.

Current status: **Phase 3 core implemented; opening escape event pending**.
The game builds and runs a 640×480 shell with movement/collision, the original
player sprite and facing frames, keys/doors/stairs, live HUD, inventory and
equipment, normal battles, a Lua event runtime with message/choice dialogs,
floors 1–10 NPC interactions, the floor-10 Golden Knight/fairy sequence,
save0 checkpoint writing, save/load slots, original save parsing and
deterministic replay.  The opening escape/cutscene event is in progress: the Zeno and prisoner
sequences plus actor/image-cutscene support are implemented; the one-time
launch `open0.bmp` sequence and exact actor timing remain Phase 3.5 work.  Ranking writes remain
read-only; floors 11–50 are Phase 4.

Start here:

* [`docs/design/00-design.md`](docs/design/00-design.md) — PoC design:
  640×480 layout, asset pipeline, text policy, architecture and milestones
* [`docs/re/README.md`](docs/re/README.md) — reverse-engineering overview
* [`docs/re/02-map-format.md`](docs/re/02-map-format.md) — map/save layout
* [`docs/re/03-state-and-save.md`](docs/re/03-state-and-save.md) — state fields and saves
* [`docs/re/04-game-logic.md`](docs/re/04-game-logic.md) — input, movement, items, battle
* [`docs/re/06-data-tables.md`](docs/re/06-data-tables.md) — monster/item tables
* [`docs/re/07-method-table.md`](docs/re/07-method-table.md) — function map
* [`docs/re/08-implementation-plan.md`](docs/re/08-implementation-plan.md) — implementation phases
* [`docs/re/10-events-and-items.md`](docs/re/10-events-and-items.md) — events, timelines, items
* [`docs/re/11-event-analysis-plan.md`](docs/re/11-event-analysis-plan.md) — plan for complete trace-free event/trigger analysis
* [`docs/re/12-event-vm.md`](docs/re/12-event-vm.md) — recovered timeline VM opcodes and cursor semantics
* [`docs/re/13-event-triggers.md`](docs/re/13-event-triggers.md) — trigger switch tables and event call graph
* [`docs/re/14-event-oracle-contract.md`](docs/re/14-event-oracle-contract.md) — event oracle ABI, snapshot and stub contract
* [`docs/re/15-event-flags.md`](docs/re/15-event-flags.md) — pilot event flags and state-index corrections
* [`docs/re/16-event-pilot.md`](docs/re/16-event-pilot.md) — P3 first-pass floor-10/roujin/opening append lift

Converted game assets are not stored in this repository; the final build
includes them and the conversion scripts live in `tools/` and
`docs/re/tools/`.  Raw extracted text stays outside the repository (default
`$XDG_CACHE_HOME/tsw/messages.raw.tsv`); refined text will be committed under
`text/` once its format is stable.

## Build (Linux)

Requirements: CMake ≥ 3.20, Ninja (or Make), a C11 compiler, SDL3 ≥ 3.2,
SDL3_image ≥ 3.0 and SDL3_ttf ≥ 3.0.  Lua 5.4.9 is vendored under
`third_party/lua`.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Generate the local asset tree once (paths are machine-specific):

```sh
GAME="$HOME/.wine/drive_c/Program Files (x86)/Tower of the Sorcerer"
python3 tools/convert_assets.py --game-dir "$GAME"
python3 tools/extract_maps.py --map-block /path/to/initial_map_block.bin
./build/tsw-re
```

New Game uses the pristine `data/maps/initial.bin` extracted from the
`syokidata` initial-map block.  A map copied from a progressed save is debug
data only and must not be used as the New Game map; `extract_maps.py --save`
writes it to `save-map.bin`.

`Chtable.oz`/`Chtableb.oz` are 8-bit BMP containers despite the extension;
the converter decodes them directly, so no pre-extracted atlas is required.
The converter also extracts the original `TSW.exe` icon into the ignored
`data/gfx/Icon.png`, which is used as the SDL window icon.
`data/` is generated and ignored by Git.  `Chtableb.png` is exported but not
yet used by the renderer; masked/overlay sprite rendering is deferred.

The default data directory is auto-detected: `./data`,
`<exe>/../share/tsw`, `<exe>/data`, then `../share/tsw`.  `--data-dir` overrides
it.  The installed build therefore runs from any working directory.

Controls: arrows move; `Enter` opens the inventory when idle, then arrows
select a special-item slot, `Enter` uses it and `Esc` closes; during an event
`Enter` advances messages, arrows choose options and `Enter` confirms; `F10`
opens the menu; `F2` starts a new game after confirmation; `F9` quits; `Esc`
cancels/closes.  The menu has Continue, New Game, Save, Load and Quit.
Save/Load use slots 1–8 in the XDG save directory.  Normal monsters are
fought automatically when walked into; results and rewards go to the console.

Useful startup options:

```sh
./build/tsw-re --inspect-save "$GAME/Savedat/save1.dat"
./build/tsw-re --save "$GAME/Savedat/save1.dat"
./build/tsw-re --save-dir /tmp/tsw-saves
./build/tsw-re --replay /tmp/replay.txt
./build/tsw-re --screenshot /tmp/tsw.png
```

Replay scripts are deterministic command files:

```text
load /path/to/save1.dat
expect floor 45
move right
expect floor 46
print
```

Other commands: `new`, `use <slot>`, `save <path>`,
`expect <field> <value>`, `expect slot<N> <value>` and
`expect tile <floor> <index> <value>`.

Replay mode is deterministic for movement, combat, item use and the static
floor-3/5/9 wall events.  It does **not** run Lua dialogue/choice/boss events
yet; hitting an unsupported event makes the replay fail explicitly instead of
silently skipping it.

The 640×400 original playfield is at the top, with an 80 px console below.
