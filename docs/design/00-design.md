# Tower of the Sorcerer — PoC design

Status: **Phase 3 implemented; Phase 3.5 nearly done**.  The
design below remains authoritative.  Phase 3.5 covers the original trace and
implementation of the opening cutscene/actor sequence; Phase 4 remains the
later floors and area bosses.

## Decisions from the project owner

1. **Scope:** first 10 floors first; expand to all 50 after the vertical slice.
2. **Assets:** reuse the original assets with format conversion; generated
   assets live in `data/` and are not committed.  The final build includes
   them; conversion scripts live in the repository.
3. **Text:** keep the raw extraction outside the repository
   (`$XDG_CACHE_HOME/tsw/`); commit the refined original text under `text/`
   once its format is stable.
4. **Saves:** full compatibility with the original `saveN.dat` format.
5. **Art:** reuse assets as-is plus format conversion.
6. **Platform:** Linux first; C11 + SDL3 + CMake for easy porting later.
7. **Audio:** deferred until the mechanics are reimplemented.
8. **This step:** design first, then implementation.

## Resolution and layout

Use a logical **640×480** canvas (4:3), with the original **640×400**
playfield unscaled and an **80 px bottom console**.  There is **no permanent
menu bar**; the game is fully keyboard-controllable and menus are keyboard
overlays.

Asset sets:

* `Data/` is the **640×400** set.  Use it for the first implementation:
  `Left.bmp`, `Right.bmp`, `CENTER.BMP`, `title05.bmp`, `Chtable.oz`,
  `Chtableb.oz` (the sprite bitmask for `Chtable`), `Key.oz`, `open*.bmp`.
* `Data3/` is the **800×500** set.  Its atlases are upscaled; do not use
  them.
* `LeftR.bmp`, `RightR.bmp`, `title05R.bmp` are **alternate art for a new,
  harder run after the first completion** — not 800×500 variants.  Keep them
  for a later hard-mode/second-run feature; do not use them in the first
  slice.

Layout:

```
640×480 logical canvas
+--------------------------------------------------------------+
| left panel     | center 390 px: 352×352 map   | right panel   |
| 125 px         | centered (19 px margins)     | 125 px        |
|                |                             |               |
| 400 px tall (original playfield, unscaled)                   |
+--------------------------------------------------------------+
| bottom console: messages / combat log / item details  80 px  |
+--------------------------------------------------------------+
```

Total: `400 + 80 = 480`.

* The original left/center/right art is drawn at `y=0` without scaling.
* The console extends the original bottom status bar: timestamped lines
  (`HH:MM:SS`), keeping the last 3–4 messages/combat results/item details.
* Controls: arrows move; `Enter` opens the inventory when idle, uses the
  selected item, or confirms a text pop-up; while the inventory is open,
  arrows select slots, `Enter` uses, and `Esc` closes; map movement is
  disabled while the inventory is open.  `Esc` otherwise cancels/closes;
  `F10` opens the menu overlay; `F2` new game; `F9` quit.  `Enter` is **not**
  a map-interaction key — map actions are arrow-only.
* Window is resizable; the canvas scales by integer factors (1×, 2×, 3×)
  with letterboxing on non-4:3 displays.  No smooth/fractional scaling by
  default.

### Menu overlay

The original Windows menu bar is replaced by a keyboard-only overlay:

```
+------------------------------+
| Continue                     |
| New Game                     |
| Save          > slots 1..8   |
| Load          > slots 1..8   |
| Options       > controls, display, sound (later)            |
| Ranking                      |
| About                        |
| Quit                         |
+------------------------------+
```

It is opened with `F10` (or `Esc` when no message/inventory is open), closes
with `Esc`, and is navigated with arrows + `Enter`.  If you prefer no overlay
at all, the same actions can be assigned to direct function keys; the overlay
is mainly for discoverability and grouping save/load/options.

## Asset pipeline

```
original installation
  Data/ Data2/ Data3/ Midi/ Savedat/
        |
        |  tools/convert_assets.py, tools/extract_text.py, ...
        v
  data/ (generated, gitignored)          text/ (refined, committed when ready)
    gfx/*.png                              messages.tsv
    maps/*.bin / *.tsv                     mapping.tsv
    audio/*.ogg (later)
        |
        |  CMake install()
        v
  final build/data/  (assets included in the distributed build)
```

* Converted assets are **not committed**.  The build/install rules copy them
  into the final build.
* Raw extracted text is not committed; it stays outside the repository
  (`$XDG_CACHE_HOME/tsw/`).  Refined original text may be committed under
  `text/` later, when the format is stable.
* Conversion scripts are committed and documented; they read the user's
  installation.
* Preferred target formats: PNG for graphics, OGG for audio later, TSV for
  generated tables, plain text for messages.
* The `.oz` tile/key atlases are 8-bit indexed BMP containers despite the
  extension.  The conversion script decodes them (and the `Chtableb` bitmask)
  into PNG; the BMP assets can be converted immediately.
* Use the `Data/` 640×400 set only.  Do not use the `Data3/` 800×500 set.
* `*R.bmp` is alternate hard-mode art for a later second run, not part of the
  first slice.

## Runtime resource tree

All runtime resources live under one root:

```
data/
  gfx/                 converted PNGs
  text/messages.tsv    generated from the refined text/ copy
  maps/                generated map/table data
  events/*.lua         installed from src/events/
  audio/               later
```

Lookup order:

1. `--data-dir` command-line option.
2. `./data` in the source/build tree (development).
3. `<exe>/../share/tsw` (CMake install prefix).
4. `<exe>/data` (portable copy next to the executable).
5. `../share/tsw` from the current directory.
6. A clear error if required resources are missing.

CMake installs `data/` (including the Lua scripts) into
`<prefix>/share/tsw`, alongside the Lua license under
`<prefix>/share/doc/tsw-re/lua`.  An installed-build smoke test must run from
an unrelated working directory without the source tree.

Saves are user data, not assets.  Default to an XDG data directory:
`$XDG_DATA_HOME/tsw` or `~/.local/share/tsw` (no `-re` suffix), with
`--save-dir` and import/export support for original `Savedat/` files.  The
on-disk save format remains the original one.

## Text

* Extract the original messages from the DFM/EXE with a robust string-list
  parser, not a heuristic scanner.  The raw extraction is written outside the
  repository, by default to `$XDG_CACHE_HOME/tsw/messages.raw.tsv`, and is
  **not** committed.
* Refine the text into `text/messages.tsv` with runtime message IDs and the
  source list/ordinal.  Commit the refined file only when the format is
  stable; raw extraction stays outside the repository.
* `text/` is committed **refined source**.  Raw extracted text is never
  placed in the repository or its Git history.
* Committing original refined text is a project-owner decision made later;
  distribution rights remain the project owner's responsibility.
* The runtime message IDs and DFM ordinals are different; build a verified
  mapping table before using numeric IDs directly.
* The PoC renders text with SDL_ttf and discovers a system font in this
  order: **Liberation Sans → DejaVu Sans → Verdana**.  Provide a `--font`
  override and a clear error if none is found.  Do **not** bundle a font in
  the first build; revisit bundling only for handhelds or minimal systems.

## Technical architecture

```
CMakeLists.txt
src/
  main.c            SDL init, window, main loop
  app.c/h           application lifecycle
  state.c/h         GameState, event flags, original state block
  map.c/h           map records, tiles, stair pairs
  render.c/h        canvas, sprites, scaling, panels
  input.c/h         keyboard/mouse mapping
  ui.c/h            HUD, panels, console, dialogs
  menu.c/h          keyboard menu overlay
  items.c/h         inventory, equipment, consumables, special items
  battle.c/h        monster table, turn simulation, info window
  events.c/h        Lua state, builder/event API, legacy coroutine runner (unused)
  event_vm.c/h      original 6-byte command VM (01-event-system.md §7)
  event_builders.c/h, event_engine.c/h  builder runtime, Timer2 tick, dialogues
  events/*.lua      event/NPC/boss scripts (committed source)
  save.c/h          original save/option/ranking formats
  assets.c/h        runtime asset loading and fallbacks
  text.c/h          message table and rendering
  audio.c/h         deferred (stub)
third_party/lua/    vendored Lua 5.4 (MIT)
tools/
  convert_assets.py
  extract_text.py
  extract_maps.py
  extract_sprites.py
  extract_timelines.py
text/
  messages.tsv      refined, committed when ready
data/               generated, gitignored
```

* **C11 + SDL3**; SDL3_image and SDL3_ttf in the first milestone; SDL3_mixer
  later.
* **CMake** with `install()` rules that copy `data/` into the final build.
* **GameState** mirrors the original 684-byte state block so original saves
  can be read/written.
* **Map** is the original 51×121 tile records plus the two-byte stair pairs.
* **Events:** the original 6-byte command VM re-implemented in C, driven by
  Lua 5.4 builder scripts (vendored under `third_party/lua`) that emit the
  original commands, plus TSV static data.  The coroutine API (messages,
  choices, battles, waits) is transitional; no event script uses it now.
* **Rendering** uses the 640×400 assets unscaled at 32×32; nearest-neighbor
  integer scaling; preserve transparency/palette conversion in the asset
  pipeline.
* **Menu** is custom-drawn; no native menu dependency.  Keyboard shortcuts
  (`F2`, `F9`, `Alt+G/O/H`) are preserved.

## Data model

Core generated tables:

* `maps` — 51 × 121 tiles + 50 stair pairs.
* `monsters` — 33 records `[hp, attack, defense, gold]` and names.
* `items` — consumables, 5 swords, 5 shields, 15 special items.
* `events` — per-floor event definitions/timelines.
* `messages` — text and IDs.

`GameState` fields follow `docs/re/03-state-and-save.md`: life, attack,
defense, gold, floor, floor_limit, x, y, keys, equipment, 15 special slots,
weapon/shield flags, event flags, option flags, checksums, magic/version.

### Event implementation: Lua + TSV

The original event system is a fixed 6-byte timeline VM (`stackwork`,
`0x4526c0`).  The reimplementation uses **Lua 5.4 event scripts + TSV static
data**:

* Lua is vendored under `third_party/lua` (MIT) and linked as a static
  library.
* Event/NPC/boss scripts live in `src/events/` and are installed with the
  build.
* The C API exposes messages, choices, battles, map/state changes, gold,
  life, floor transitions, waits, sound and sprites.
* Lua coroutines yield **typed wait requests** (`TICKS`, `MESSAGE`,
  `ANIMATION`, `CHOICE`, `BATTLE`).  The scheduler resumes only when the
  request is satisfied; choice/battle results are pushed back to Lua.  Use a
  fixed **60 Hz simulation tick**; `wait(n)` waits `n` ticks.  Rendering may
  run independently, and tests advance the simulation clock without
  rendering.
* A TSV-only action runner is the documented fallback if Lua embedding
  proves problematic.

**Revised:** the bullets above describe the original coroutine runtime, which
is still in the tree but unused.  The target execution model is the command VM in C with Lua
builders (`emit(op, arg1, arg2)`), stepped by a `timer2`-equivalent tick rather
than a fixed 60 Hz `wait(n)`; ported events are accepted by byte-for-byte
parity with native-oracle golden vectors.

See [`01-event-system.md`](01-event-system.md) for the full comparison,
original VM description, and implementation details (section 7 for the
revision).

## Save compatibility

* Read and write the original 6957-byte `saveN.dat` format.
* Normal slots: verify the magic `-4`, version `12`, and both checksums.
* `save0.dat` is the fairy checkpoint with **zero checksums** in the
  original data.  Use a separate checkpoint validation profile (initially
  read-only) and confirm the original load rules before writing it.
* Preserve the map block, stair pairs, state block, and trailing metadata.
* Ranking files `option1..5.dat` and `option.dat` are supported.
* Save is allowed only at **quiescent boundaries** (normal exploration).
  The Save menu is disabled while a Lua event, dialogue, choice, or battle
  is active.  Loading/new game cancels all Lua coroutines and pending
  actions and reconstructs from the saved state.
* Differential tests: load each original save, perform a scripted action,
  compare state/map bytes, and test saving during dialogue/battle (must be
  rejected safely) and loading while an event is suspended (must cancel it).

## Milestones

0. **Design** (this document).
1. **Skeleton + pipeline:** CMake, SDL3 window, 640×480 canvas, asset
   converter, text extractor, placeholder/real asset loading.
2. **Core exploration:** map, movement, doors/keys, stairs, HUD, original
   save/load; floors 1–10.
3. **Items and combat:** consumables, equipment, 15 special items, battle
   simulation, monster info/forecast.
3.5 **Ranking analysis/fixtures:** resolve the area-1 fairy score formula and
   the ranking-file sixth field/footer before implementing ranking writes.
   If unresolved, ship ranking as read-only for the slice.
4. **NPCs/events/bosses (floors 1–10):** merchants, old men, altars,
   floor-10 boss/fairy, area-1 ranking, `save0` checkpoint.
5. **Vertical-slice polish:** menus, options, console layout, full
   floor-1–10 walkthrough.
6. **Full game:** remaining event timelines, floors 11–50, endings, audio.

## Open design decisions

Resolved:

* **Layout:** 400 px playfield + 80 px console; no permanent menu bar.
* **Input:** keyboard-only; map actions are arrows, `Enter` only for items/
  dialogs.
* **Menu access:** `F10` keyboard overlay (Continue/New Game/Save/Load/
  Options/Ranking/About/Quit); no difficulty entry.
* **Font:** Liberation Sans, fallback DejaVu Sans, then Verdana.  Do not
  bundle a font in the first build; revisit bundling only for handhelds or
  systems without the fallback chain.
* **Save directory:** `$XDG_DATA_HOME/tsw` or `~/.local/share/tsw`.
* **`.oz` decoding:** they are BMP containers; convert them in the conversion
  script; converted PNGs go to `data/`, not the repository.
* **Text extraction:** raw extraction stays outside the repository
  (`$XDG_CACHE_HOME/tsw/`); refined text is committed in `text/` only when the
  format is stable.
* **Event implementation (revised):** command VM in C + Lua 5.4 builder
  scripts + TSV static data; Lua is vendored under `third_party/lua`; scripts
  live in `src/events/`.  See [`01-event-system.md`](01-event-system.md) §7.

Still open:

* **Hard-mode alternate art:** `*R.bmp` is reserved for a second, harder
  run; decide when to expose that mode.

## Testing strategy

* Save round-trip and checksum validation against all eight original slots.
* Differential state tests: scripted movement/actions vs original under Wine.
* Map rendering tests by tile ID, not pixels.
* Battle-table tests for every monster, including cross/Dragon Slayer.
* Event checklist: floors 1–10 NPCs, boss, fairy/ranking, save0.
* Event golden vectors: for each ported builder, compare command words and
  state/map/aux effects with the native oracle on identical snapshots
  (generated locally; skipped when absent).
* Asset pipeline test: clean source install -> converted data -> playable
  build with no missing files.

## Non-goals for the first milestone

* Audio playback (deferred).
* Full 50-floor content.
* Pixel- or frame-exact rendering parity.  (Event command order and tick
  parity with the original VM *is* a goal; see `01-event-system.md` §7.)
* Native platform menus or the original Windows font.
