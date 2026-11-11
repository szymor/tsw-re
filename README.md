# tsw-re

Reverse-engineering notes and a planned C + SDL3 re-implementation of
**Tower of the Sorcerer v1.2**.

Current status: **Part 1 skeleton implemented**.  The game builds and runs a
640×480 shell with the floor-1 map, original save parsing, Lua embedding and
a keyboard menu stub.  Gameplay work starts in Phase 1.

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

`Chtable.oz`/`Chtableb.oz` are 8-bit BMP containers despite the extension;
the converter decodes them directly, so no pre-extracted atlas is required.
`data/` is generated and ignored by Git.  `Chtableb.png` is exported but not
used by the Part 1 renderer; masked/overlay sprite rendering is deferred.

The default data directory is auto-detected: `./data`,
`<exe>/../share/tsw`, `<exe>/data`, then `../share/tsw`.  `--data-dir` overrides
it.  The installed build therefore runs from any working directory.

Part 1 controls: arrows move the debug player; `F10` opens the keyboard menu;
`Enter` is reserved for item use/dialog confirmation; `Esc` closes the menu.
The 640×400 original playfield is at the top, with an 80 px console below.

Useful startup options:

```sh
./build/tsw-re --inspect-save "$GAME/Savedat/save1.dat"
./build/tsw-re --save "$GAME/Savedat/save1.dat"
./build/tsw-re --screenshot /tmp/tsw.png
```
