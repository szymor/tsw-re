# The remake

*Tower of the Sorcerer: Revisited* 1.0 (`src/version.h`; the program is
`tsw-re`) is the whole of *Tower of the Sorcerer* 1.2 in C11 and SDL3:
floors 1–50, both endings, rankings, saves compatible with the original's,
in English, Japanese, Polish, Russian and Vietnamese, for Linux and
Windows.  Every event runs on a C port of the original command VM, driven
by Lua builders ported from the original handlers and checked against the
original's own code (`16-event-pilot.md`).  This note is the map from the
remake to the reverse-engineering notes; how to build and play is in the
README.

## Goals

* The **gameplay** of Tower of the Sorcerer 1.2, exactly: event command
  order, state, map and timing follow the original; pixel-exact drawing
  is not a goal.
* A **640×480 canvas**: the original 640×400 playfield unscaled, an 80 px
  console below it, no permanent menu bar; keyboard only, menus as
  overlays.
* The original's art and sound, converted (`data/`).
* **C11 + SDL3** (SDL3_image, SDL3_ttf, SDL3_mixer), CMake, one code path
  for every platform.

## Source layout

```
src/
  main.c              command line, data/save folders, screenshot mode
  app.h/.c            application state and main loop
  state.h/.c          the 684-byte state block, checksums (03)
  map.h/.c            the 6273-byte map block (02)
  save.h/.c           saves in the original format (03)
  game.h/.c           new game, load, snapshot
  fileio.h/.c         files and folders through SDL3 (UTF-8, atomic saves)
  assets.h/.c         pictures, atlases and fonts
  render.h/.c         the canvas, panels, map, console
  render_windows.h/.c the original's windows (19), the menus, About
  widgets.h/.c        the remake's window style; justified text
  text_layout.h/.c    word wrap and justification (pure logic)
  pictures.h/.c       the words on the intro's and ending's pictures
  input.h/.c          keys, menus, movement
  ui.h/.c             menu bar, submenus, confirmations
  options.h/.c        options.ini (sound, music, speed, text size, language)
  ranking.h/.c        Form2's ranking files (03)
  messages.h/.c       the game text, data/text/<lang>/ (18)
  itext.h             the remake's own interface text and its keys
  log.h/.c            the console history
  items.h/.c          item and equipment names and icons
  battle.h/.c         the Orb of the Hero's rows; monsters.c: the table (06)
  intro.h/.c          the open0.bmp launch intro
  audio.h/.c          sounds and tunes (20)
  replay.h/.c         deterministic replay scripts, headless
  events.h/.c         the event runtime wrapper
  event_vm.h/.c       the original's 6-byte command VM (12)
  event_builders.h/.c the Lua builder runtime
  event_engine.h/.c   Timer2, dialogues, choices, presentation
  version.h           title, version, licence
  events/*.lua        one builder per original handler, by family
third_party/lua/      Lua 5.4 (MIT)
tools/                the scripts that made data/; the Windows build
docs/re/tools/        the analysis tools and the oracle
data/                 everything the game loads (data/README.md)
tests/                unit tests and the parity tests; tests/vectors/
```

The state is kept as the original's 171 ints (`StateBlock`, the indices
of `03-state-and-save.md`) and the map as its 6273 bytes, so saves are
read and written as they are.

## How the remake presents the original

The events change state and append commands exactly as the original does;
what follows is how the remake draws what they do, each from the
original's drawing code.

* **Status bar.**  `disp` (`0x44cb34`): sprite 1 shows the status string
  `0x4b8684`, any other sprite `ListBox2[sprite + 0x7f]`; op `0x02` sets
  the sprite and calls it.  The remake has no Edit8: each new text goes
  into the console history (timestamped, PageUp/PageDown scroll back),
  once per action.  Battle lines are built as `taisen` (`0x44aac1`) /
  `taisen2` (`0x47ff1e`) do, `ListBox2[0xc7]` " " name ". " `[0xc8]` " "
  gold `[0xc9]` (`[0xf0]` and half the gold on the octopus' outer cells);
  the remake's version also names the vital power lost.
* **Floor change wipe.**  Op `0x15` (`0x45433b`): mode 0 covers map cell
  `order[n]` with ImageList tile `0x9e`, mode 1 redraws it from the new
  floor; two cells per step in the spiral order at `0x489b20`.  Only the
  reveal pass runs `timerin` when its last cell is `0x79` (`0x4547b4`), so
  Timer2 stays at the stairs speed until the floor is complete; mode 0's
  first step plays the stairs wave when the music is off.  The wings and
  the Orb of Flying redraw through `mhyouji` without a transition.
* **Battle hits and the monster panel.**  Op `0x0e` (`0x453a36`, the
  octopus' and the dragon's last blow) inlines op `0x09`'s `kasanegaki`
  composite before its damage.  `monshyouji` (`0x44b78c`) fills the right
  panel: the monster through its mask over panel tile `0xa2`, its name
  (`ListBox2[index + 0x5e]`), life/offense/defense × (difficulty + 1);
  ops `0x05` and `0x0e` update the life.  With no monster the panel shows
  `ListBox2[0xd6..0xd8]` and the name `[0x5d]`.
* **The opening.**  `openning9` phase 0 (`0x482e60`) composes every frame
  with `kasanegaki`: `open1.bmp` through a 20×13 grid of masks
  `ImageList[frame − 0x42]` (frames `0xe8..0xf7`) over the bitmap
  `0x48c528`, which still holds the bare panels `syokidata2` copied there;
  frame `0xe8` hides the panels, phase 1 reveals `open2`.  Sub `0xf`
  (`0x4539be`) captures the form into `0x48c528`, which the second run's
  ending dissolves over.  Sub `7` (`0x45345c`) puts `title05.bmp`
  (`title05R.bmp` on a second run) over the map until the dissolve.
* **Two-frame animation.**  `mhyouji` keeps two map buffers, `0x48c514`
  (tile − 1) and `0x48c518` (tile − 1 + `imagework`'s offset, `0x454c10`;
  `event_engine_map_tile`); `Timer1Timer` (`0x443120`) shows one per tick
  and redraws the hero standing in it.  After a step both hold the walking
  poses, so the hero walks for two ticks, then stands.  Timer1 stops in
  `openning9`, sub `7`, sub `0xd`, the stairs (music on) and the Orb of
  the Hero, and starts again at op `0x15`'s last reveal step, the end of
  the opening, the Orb's window closing and `syokidata2`.
* **Endings and rankings.**  The floor-50 ending runs `openning9`'s
  phases: sub `0xe` hides the panels' values, sub `0xf` captures the
  screen, phase 2 dissolves `open0` over it, phase 6 shows `open0` and
  `End05..End20`, phase 4 shrinks `End20` over white, phase 5 reveals the
  closing text over `End26`, phase 7 "The End", phase 8 the button;
  clicking it (`image34click`) runs `0x0b/6` and game over.  `yousei`
  reads and checks `optionN.dat`; Form2 asks for the name and writes the
  files (`03-state-and-save.md`).
* **Windows.**  Every window sits where TSW.exe puts its controls at run
  time in the 640×400 layout (`19-windows.md`, `form_layout.py`), in the
  remake's plain style: the message window, the item question, the Orb of
  Flying's floor, the Orb of Wisdom's list, the Orb of the Hero's panel,
  MessageDlg/ShowMessage and Form2 with its name prompt.  Questions open
  on their no-action answer.  The remake keeps its own 3×5 inventory
  instead of the original's overlay cursor (`itemichiwork`).
* **Sound.**  The WAV sounds and the tunes (pre-rendered from MIDI to Ogg)
  through SDL3_mixer, as op `0x01`, op `0x0f`, `soundcheck`/`soundplay`
  and the switches `state[0x59]`/`[0x5a]` drive them (`20-audio.md`).

Engine rules for every event: input is locked while the command count is
non-zero; saving is refused during an event; load and new game clear the
buffer, the auxiliaries and all presentation state.  A path that is not
ported calls `fail()`, which restores the state and logs it; an unported
`0x0b` splice is logged, skipped and counted (`EventEngine.unported`).
None is reached by any vector.

## Testing

All in `ctest`; the parity tests read the original's recorded results in
`tests/vectors` (`export_event_vectors.py --repo` remakes them) and fail
when they are missing.

* **Event parity** (`test_event_builders`): each of the 613 vectors runs
  its builder and must give the original's command words, state, map and
  auxiliaries; 609 also run their timeline to the end through the engine
  (dialogues confirmed, Timer2 ticking) and must end in the original's
  state and map; every vector must show the original's status bar (the
  reworded victory line keeps its first entry, monster and gold).  They
  cover every `handan` raw value (`action_*`), every monster tile, on the
  second run and with the Lucky Gold (`monster_all_*`), every NPC, floor
  event, item and the endings.
* **Coverage**: `check_event_coverage.py --require <handler>` checks the
  executed blocks, edges and append sites against the static CFG
  (`16-event-pilot.md` §9).
* **The original's data** (`test_original`): the monster table read from
  TSW.exe; `mhyouji`'s drawing of the 50 floors and of every map value in
  both frames; `savework`'s bytes and `loadwork`'s verdict and loaded game
  (also with a byte changed).
* **Unit and regression tests**: the VM, the engine (one-shot events after
  save/load, the same commands at any frame rate, load/new game in the
  middle of a timeline), replay (the opening and a battle, headless),
  saves and checksums, the ranking files, text layout, the input handler.

After a change the execution oracle also models, re-export the vectors so
the drains cover it (`docs/re/tools/README.md`).

## Decisions (from the project owner)

1. **Assets:** the original's 640×400 `Data/` set, converted and committed
   in `data/` (NAO allows works based on the game); `Data3/` (800×500,
   upscaled) is not used; the `*R.bmp` art shows on a second run, as in the
   original.  The scripts that made `data/` stay in `tools/`.
2. **Text:** `data/text/<lang>/`, one folder per language, edited and
   translated there; a line's number is the original's message id.
3. **Saves:** the original's `saveN.dat` format, read and written as is.
4. **Platforms:** Linux and Windows (cross-compiled with MinGW-w64), no
   platform branches in the code; no macOS build.
5. **Layout and input:** 640×480 = 400 px playfield + 80 px console;
   keyboard only, menus as overlays, no accelerators.
6. **Font:** Liberation Sans, bundled (OFL); the system's copy first on
   Linux; Japanese uses the system's CJK font.
7. **Player folder:** `SDL_GetPrefPath(NULL, "tsw")` (`~/.local/share/tsw`,
   `%APPDATA%\tsw`) for the saves and `options.ini`; `--save-dir` moves the
   saves.
8. **Events:** the original's command VM in C, the handlers as Lua 5.4
   builders (vendored), accepted only on parity with the original
   (`12-event-vm.md` §13).
9. **Licence:** the program is GPL-3.0-or-later; the original's material
   in `data/` and `tests/vectors/` is not (README, *Licence*).
