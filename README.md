# Tower of the Sorcerer: Revisited

Version 1.0.  A C + SDL3 re-implementation of **Tower of the Sorcerer v1.2**,
with the reverse-engineering notes it is built from.  The program is
`tsw-re`.

Current status: **Phases 0–3.5 done; Phase 4 (floors 11–50) in progress.**
The game builds and runs a 640×480 shell with movement/collision, the original
player sprite and facing frames, keys/doors/stairs, live HUD, inventory and
equipment, normal battles, save/load slots, original save parsing, `save0`
checkpoints, deterministic replay and the `open0.bmp` launch intro.  Every
event runs on a C port of the original 6-byte command VM, driven by Lua
builders ported from the original handlers and checked against the original
code in an x86 emulator (golden vectors plus execution-oracle drains): the
floor-3 opening, old men, merchants, altars, secret walls, the thief, the
fairy, all `ichicheck` floor events and event battles.  Ranking writes remain
disabled.  What is left is listed in
[`docs/re/08-implementation-plan.md`](docs/re/08-implementation-plan.md)
(Phase 4).

Start here:

* [`docs/design/00-design.md`](docs/design/00-design.md) — PoC design:
  640×480 layout, asset pipeline, text policy, architecture and milestones
* [`docs/re/README.md`](docs/re/README.md) — reverse-engineering overview
* [`docs/re/02-map-format.md`](docs/re/02-map-format.md) — map/save layout
* [`docs/re/03-state-and-save.md`](docs/re/03-state-and-save.md) — state fields and saves
* [`docs/re/04-game-logic.md`](docs/re/04-game-logic.md) — input, movement, items, battle
* [`docs/re/06-data-tables.md`](docs/re/06-data-tables.md) — monster/item tables
* [`docs/re/07-method-table.md`](docs/re/07-method-table.md) — function map
* [`docs/re/08-implementation-plan.md`](docs/re/08-implementation-plan.md) — implementation phases and what is left
* [`docs/re/09-open-questions.md`](docs/re/09-open-questions.md) — unresolved details
* [`docs/re/10-events-and-items.md`](docs/re/10-events-and-items.md) — events, timelines, items
* [`docs/re/11-event-analysis-plan.md`](docs/re/11-event-analysis-plan.md) — plan for complete trace-free event/trigger analysis
* [`docs/re/12-event-vm.md`](docs/re/12-event-vm.md) — recovered timeline VM opcodes and cursor semantics
* [`docs/re/13-event-triggers.md`](docs/re/13-event-triggers.md) — trigger switch tables and event call graph
* [`docs/re/14-event-oracle-contract.md`](docs/re/14-event-oracle-contract.md) — event oracle ABI, snapshot and stub contract
* [`docs/re/15-event-flags.md`](docs/re/15-event-flags.md) — event flags and state-index corrections
* [`docs/re/16-event-pilot.md`](docs/re/16-event-pilot.md) — pilot, execution-oracle results, coverage matrix and ported handlers
* [`docs/re/17-static-findings.md`](docs/re/17-static-findings.md) — findings from the Ghidra annotation pass
* [`docs/re/18-messages.md`](docs/re/18-messages.md) — dialogue text: `ListBox2` and the `zyouout` index rules

Converted game assets are not stored in this repository; the final build
includes them and the conversion scripts live in `tools/` and
`docs/re/tools/`.  Raw extracted text stays outside the repository (default
`$XDG_CACHE_HOME/tsw/messages.raw.tsv`); refined text will be committed under
`text/` once its format is stable.

## Build (Linux)

Requirements: CMake ≥ 3.20, Ninja (or Make), a C11 compiler, SDL3 ≥ 3.2,
SDL3_image ≥ 3.0, SDL3_ttf ≥ 3.0 and SDL3_mixer ≥ 3.0 (Ogg Vorbis and WAV).
Lua 5.4.9 is vendored under `third_party/lua`.  The asset converter needs
Pillow, and for the music FluidSynth, ffmpeg (libvorbis) and a General MIDI
SoundFont.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Without system SDL3 packages (CI, containers, sandboxed VMs), build SDL into
a private prefix and test headless:

```sh
tools/build_sdl3_headless.sh                 # -> ~/opt/sdl3
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=~/opt/sdl3
cmake --build build
LD_LIBRARY_PATH=~/opt/sdl3/lib SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    ctest --test-dir build --output-on-failure
```

Generate the local asset tree once (paths are machine-specific):

```sh
GAME="$HOME/.wine/drive_c/Program Files (x86)/Tower of the Sorcerer"
python3 tools/convert_assets.py --game-dir "$GAME"
python3 tools/extract_maps.py --map-block /path/to/initial_map_block.bin
python3 tools/extract_messages.py --game-dir "$GAME"   # dialogue text
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
The converter copies the original `Wav/` sounds to `data/audio/` and renders
the `Midi/` tunes to Ogg Vorbis there (FluidSynth, then ffmpeg; the game
plays no MIDI itself).  The SoundFont is `--soundfont`, `$TSW_SOUNDFONT`, or
one from `/usr/share/soundfonts` (HiDef first); `--no-audio` skips this.
In the game, `--no-wav` / `--no-bgm` turn the sounds / music off, and
`TSW_NO_AUDIO=1` keeps the audio device closed.
`data/` is generated and ignored by Git.  `tools/check_assets.py
--game-dir "$GAME"` checks the tree: required files, picture sizes, every
converted picture against its original pixel for pixel, the sprite mask,
the sounds and tunes.

The default data directory is auto-detected: `./data`,
`<exe>/../share/tsw`, `<exe>/data`, then `../share/tsw`.  `--data-dir` overrides
it.  The installed build therefore runs from any working directory.

Controls (keyboard only, no shortcuts for menu entries): arrows move —
holding one walks on at the NPCs' pace, one cell per Speed interval
(150 / 250 / 350 ms), with no repeat delay;
`Enter` opens the inventory when idle, then arrows select a special-item
slot, `Enter` uses it and `Esc` closes; in a window `Enter` confirms, arrows
choose and `Y`/`N` answer a question, which opens on its no-action answer;
`Esc` (or `F10`) opens the menu bar — Game (New game, Load, Save, Ranking,
Quit), Options (Sound effects, Music, Speed, Message text) and Help
(Controls, About) — with `Left`/`Right` between the menus.  Options are
written to `$XDG_CONFIG_HOME/tsw/options.ini` on every change.  Load also
offers the fairy's checkpoint (`save0.dat`).
Save/Load use slots 1–8 in the XDG save directory.  Every move runs the
original's tile handler (`handan`) on the command VM: walking into a monster
plays the battle turn by turn, doors and pickups animate, and the status
line goes to the console.
A new launch plays the `open0.bmp` intro; gameplay input cannot skip it
(`--no-intro` is only for deterministic screenshots).

Useful startup options:

```sh
./build/tsw-re --inspect-save "$GAME/Savedat/save1.dat"
./build/tsw-re --save "$GAME/Savedat/save1.dat"
./build/tsw-re --save-dir /tmp/tsw-saves
./build/tsw-re --replay /tmp/replay.txt
./build/tsw-re --screenshot /tmp/tsw.png --no-intro
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
`expect <field> <value>`, `expect slot<N> <value>`,
`expect tile <floor> <index> <value>`, and the answers to the windows a
move or an item can open: `choose yes|no` (merchants, altars' offer),
`altar 0..3`, `tryagain yes|no` (after a lost battle), `item yes|no` (the
item panel after `use`), `fly up|down|land` (the Orb of Flying) and
`ending` (click the ending's last picture).

Replay runs the same event engine as the game, headless: each move's
timeline is drained at once, plain dialogue windows are confirmed and the
ranking form closed; a question must be answered before the next move.

The 640×400 original playfield is at the top, with an 80 px console below.
