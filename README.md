# Tower of the Sorcerer: Revisited

Version 1.0.  A C + SDL3 re-implementation of **Tower of the Sorcerer v1.2**,
with the reverse-engineering notes it is built from.  The program is
`tsw-re`.

Status: **the whole game is playable**, floors 1–50, both endings and the
rankings, in English, Japanese, Polish, Russian and Vietnamese.  Every
event runs on a C port of the original 6-byte command VM, driven by Lua
builders ported from the original handlers and checked against the
original code in an x86 emulator (golden vectors plus execution-oracle
drains).  It builds for Linux and, cross-compiled, for Windows; what is
left is listed in
[`docs/re/08-implementation-plan.md`](docs/re/08-implementation-plan.md).

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

The converted game data and the text are committed under `data/` (see
*Data and text* below); the scripts that made them live in `tools/` and
`docs/re/tools/`.

## Credits

* *Tower of the Sorcerer: Revisited*: vamastah.
* Original game: *迷宮RPG 魔法の塔* (*Mahō no Tō*) by NAO, PC-98, 1994–1996,
  released in 1996 ([wwajp.com/mtower](https://wwajp.com/mtower/)).  NAO
  allows works based on the game, within common sense.
* Windows version, *Tower of the Sorcerer* 1.2: Oz & Kenichi; its English
  text was checked by Tween and Miki Olsz.
* Since then the tower has inspired thousands of fan-made towers, many on a
  Chinese fan forum founded in 2003.

## Licence

The repository holds two kinds of work under different terms: the
**program**, which is ours and free software under the GPL, and the
**original game's material** (its pictures, sounds, music, map and text),
which is not ours and is used with the permission of the game's author.

### The program: GPL-3.0-or-later

*Tower of the Sorcerer: Revisited* is free software: you can redistribute
it and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.  It is distributed in the
hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See [`LICENSE`](LICENSE) (SPDX: `GPL-3.0-or-later`).

This covers everything the project wrote:

* the C source (`src/`), the Lua event builders (`src/events/`) and the
  tests (`tests/`, except the recorded results in `tests/vectors/`);
* the tools (`tools/`, `docs/re/tools/`), the build files (`CMakeLists.txt`,
  `cmake/`) and the packaging (`packaging/README.txt`);
* the documentation (`README.md`, `docs/`, `data/README.md`);
* the remake's own interface text, `data/text/<lang>/interface.tsv` (menus,
  window titles, key hints, the HUD, Help > Controls and About, the log
  lines), except what the next section lists.

The program re-implements the original's behaviour (its event handlers,
formulas and tables); the GPL covers this implementation, not the
original game's design.

### The original game's material: the author's permission

These are the original game's, or made from it, and are **not** under the
GPL; this repository grants no licence to them:

| What | Where |
|---|---|
| Pictures: panels, tiles, sprites, the title, intro and ending pictures, the icon | `data/gfx/` |
| The same pictures with their lettering removed, so the game can write it | `data/gfx/*_clean.png` |
| Sounds and music (the tunes rendered from the original MIDI) | `data/audio/` |
| The New Game map | `data/maps/initial.bin` |
| The original English text: the dialogue, the code's messages, the speakers' names | `data/text/en/messages.tsv`, `ui.tsv`, `speakers.tsv` |
| The words of the original's pictures, and the names of its items, monsters and characters, wherever they appear | `picture.*`, `item.*` and similar entries of `interface.tsv` |
| The translations of all of the above (Polish, Japanese, Russian, Vietnamese) | `data/text/{pl,ja,ru,vi}/` |
| What the original's code did, recorded for the tests (its maps, commands and results) | `tests/vectors/` |

They come from *Tower of the Sorcerer* 1.2 by Oz & Kenichi, whose art is
that of NAO's *Mahō no Tō*.  They are used, edited, translated and
redistributed as NAO allows works based on the game, within common sense
([wwajp.com/mtower](https://wwajp.com/mtower/)); anyone reusing them is
bound by the same terms, not by the GPL.

### Third-party components

| What | Licence | Where |
|---|---|---|
| Lua 5.4 | MIT | `third_party/lua/` (`LICENSE`) |
| Liberation Sans and Serif fonts | SIL Open Font License 1.1 | `data/fonts/` (`LICENSE-LiberationSans.txt`) |
| SDL3, SDL3_image, SDL3_ttf, SDL3_mixer (in the Windows package) | zlib | `licenses/` in the package |
| FreeType, HarfBuzz (built into SDL3_ttf.dll) | FreeType License, Old MIT | `packaging/licenses/` |

## Build (Linux)

Requirements: CMake ≥ 3.20, Ninja (or Make), a C11 compiler, SDL3 ≥ 3.2,
SDL3_image ≥ 3.0, SDL3_ttf ≥ 3.0 and SDL3_mixer ≥ 3.0 (Ogg Vorbis and WAV).
Lua 5.4.9 is vendored under `third_party/lua`, and everything the game
loads is committed under `data/` (`data/README.md`), so nothing else is
needed.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/tsw-re
```

`cmake --install build --prefix <dir>` installs `bin/tsw-re` and
`share/tsw/`; `cpack --config build/CPackConfig.cmake` makes
`tsw-re-<version>-linux-<arch>.tar.gz` of the same (it uses the system's
SDL3 libraries).

Without system SDL3 packages (CI, containers, sandboxed VMs), build SDL into
a private prefix and test headless:

```sh
tools/build_sdl3_headless.sh                 # -> ~/opt/sdl3
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=~/opt/sdl3
cmake --build build
LD_LIBRARY_PATH=~/opt/sdl3/lib SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    ctest --test-dir build --output-on-failure
```

## Build (Windows)

The Windows build is cross-compiled on Linux with MinGW-w64.  It needs
`x86_64-w64-mingw32-gcc` (Arch: `mingw-w64-gcc`), CMake, Ninja and curl,
and wine to run the tests.  The script downloads the official SDL3 MinGW
releases once into `~/.cache/tsw/mingw`; their DLLs have their codecs,
FreeType and HarfBuzz built in, so the package needs no other DLL.

```sh
tools/build_windows.sh test package
```

This builds into `build-windows/`, runs the tests under wine and makes
`build-windows/tsw-re-<version>-windows-x86_64.zip`: a folder with
`tsw-re.exe`, the four SDL DLLs, `data\`, the licences and a README.  It
runs on Windows 10 and later.

## Data and text

`data/` holds the converted pictures, sounds and tunes, the New Game map,
the font and the text (`data/README.md`).  The text is in
`data/text/<lang>/` and is edited there.  The scripts in `tools/` made the
data from an original installation and can make it again after a converter
fix (`tools/README.md`); a build does not run them.

The data folder is found at `./data`, `<exe>/../share/tsw`, `<exe>/data`,
then `../share/tsw`; `--data-dir` overrides it.  Saves and `options.ini`
go to the player's folder: `$XDG_DATA_HOME/tsw` (else
`~/.local/share/tsw`) on Linux, `%APPDATA%\tsw` on Windows; `--save-dir`
moves the saves.

## Playing

Controls (keyboard only, no shortcuts for menu entries): arrows move —
holding one walks on at the NPCs' pace, one cell per Speed interval
(150 / 250 / 350 ms), with no repeat delay;
`Enter` opens the inventory when idle, then arrows select a special-item
slot, `Enter` uses it and `Esc` closes; in a window `Enter` confirms, arrows
choose and `Y`/`N` answer a question, which opens on its no-action answer;
`Esc` (or `F10`) opens the menu bar — Game (New game, Load, Save, Ranking,
Quit), Options (Sound effects, Music, Speed, Message text, Language) and Help
(Controls, About) — with `Left`/`Right` between the menus.  Options are
written to `options.ini` beside the saves on every change.  Load also
offers the fairy's checkpoint (`save0.dat`).
Save/Load use slots 1–8 in the player's folder.  Every move runs the
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
