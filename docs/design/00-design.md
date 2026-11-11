# Tower of the Sorcerer — PoC design

Status: **design draft for approval**.  No game code has been written yet.

## Decisions from the project owner

1. **Scope:** first 10 floors first; expand to all 50 after the vertical slice.
2. **Assets:** reuse the original assets with format conversion; generated
   assets live in `data/` and are not committed.  The final build includes
   them; conversion scripts live in the repository.
3. **Text:** store the original game text in the repository as-is, without
   paraphrasing.
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
* Controls: arrows move; `Enter` uses the selected item or confirms a text
  pop-up; `Esc` cancels/closes; `F10` opens the menu overlay; `F2` new game;
  `F9` quit.  `Enter` is **not** a map-interaction key — map actions are
  arrow-only.
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
  data/ (generated, gitignored)          text/ (committed as-is)
    gfx/*.png                              messages.tsv
    maps/*.bin / *.tsv
    audio/*.ogg (later)
        |
        |  CMake install()
        v
  final build/data/  (assets included in the distributed build)
```

* Converted assets are **not committed**.  The build/install rules copy them
  into the final build.
* The original game text is committed under `text/` as-is, per the project
  decision.  It is not paraphrased.
* Conversion scripts are committed and documented; they read the user's
  installation.
* Preferred target formats: PNG for graphics, OGG for audio later, TSV for
  generated tables, plain text for messages.
* The conversion script decodes the `.oz` tile/key atlases (and the
  `Chtableb` bitmask) into PNG; the BMP assets can be converted immediately.
* Use the `Data/` 640×400 set only.  Do not use the `Data3/` 800×500 set.
* `*R.bmp` is alternate hard-mode art for a later second run, not part of the
  first slice.

## Runtime asset lookup

1. `--data-dir` command-line option.
2. `data/` next to the executable (installed build).
3. `data/` in the source/build tree (development).
4. A clear error if required assets are missing.

Saves are user data, not assets.  Default to an XDG data directory:
`$XDG_DATA_HOME/tsw` or `~/.local/share/tsw` (no `-re` suffix), with
`--save-dir` and import/export support for original `Savedat/` files.  The
on-disk save format remains the original one.

## Text

* Extract the original messages from the DFM/EXE with a robust string-list
  parser, not a heuristic scanner.  A first pass is already in
  `text/messages.tsv` (401 strings, DFM order); it still contains some UI
  component/font strings and needs refinement.
* Store the extracted text in `text/messages.tsv` with columns for
  `id`, `text`, and the source list/ordinal.  Keep the original wording.
* The runtime message IDs and DFM ordinals are different; build a verified
  mapping table before using numeric IDs directly.
* The PoC renders text with SDL_ttf.  The original relies on Windows
  Verdana; use a free, metrically similar font (or a user-provided font)
  and keep the font file in the final build.

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
  menu.c/h          custom menu bar and menu actions
  items.c/h         inventory, equipment, consumables, special items
  battle.c/h        monster table, turn simulation, info window
  events.c/h        event table/timeline interpreter
  save.c/h          original save/option/ranking formats
  assets.c/h        runtime asset loading and fallbacks
  text.c/h          message table and rendering
  audio.c/h         deferred (stub)
tools/
  convert_assets.py
  extract_text.py
  extract_maps.py
  extract_sprites.py
  extract_timelines.py
text/
  messages.tsv
data/               generated, gitignored
```

* **C11 + SDL3**; SDL3_image and SDL3_ttf in the first milestone; SDL3_mixer
  later.
* **CMake** with `install()` rules that copy `data/` into the final build.
* **GameState** mirrors the original 684-byte state block so original saves
  can be read/written.
* **Map** is the original 51×121 tile records plus the two-byte stair pairs.
* **Events** are data-driven.  The first implementation translates the
  original timeline inventory into a high-level `(floor, mapIndex, tile)`
  event table; the original 6-byte VM is optional unless exact animation
  fidelity is needed.
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

### Event implementation (decision pending)

The original event system is a fixed 6-byte timeline VM (`stackwork`,
`0x4526c0`).  Two practical re-implementations are under consideration:

* **TSV action table** — simple C runner, no dependency, good for linear
  timelines, but needs a custom mini-language for branches/choices.
* **Lua event scripts + TSV data** — expressive control flow for NPCs and
  bosses, at the cost of embedding Lua 5.4 (MIT).

See [`01-event-system.md`](01-event-system.md) for the full comparison and
recommendation.  The event format is not finalized yet.

## Save compatibility

* Read and write the original 6957-byte `saveN.dat` format.
* Verify the magic `-4`, version `12`, and both checksums.
* Preserve the map block, stair pairs, state block, and trailing metadata.
* `save0.dat` is the fairy checkpoint; support reading it.
* Ranking files `option1..5.dat` and `option.dat` are supported.
* Differential tests: load each original save, perform a scripted action,
  and compare the state/map bytes with the original game.

## Milestones

0. **Design** (this document).
1. **Skeleton + pipeline:** CMake, SDL3 window, 640×480 canvas, asset
   converter, text extractor, placeholder/real asset loading.
2. **Core exploration:** map, movement, doors/keys, stairs, HUD, original
   save/load; floors 1–10.
3. **Items and combat:** consumables, equipment, 15 special items, battle
   simulation, monster info/forecast.
4. **NPCs/events/bosses:** merchants, old men, altars, floor-10 boss/fairy,
   ranking, `save0` checkpoint.
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
* **`.oz` decoding:** implement it in the conversion script; converted PNGs
  go to `data/`, not the repository.
* **Text extraction:** refine during implementation; keep the original text
  in `text/`.

Still open:

1. **Event implementation:** Lua event scripts + TSV data (recommended) vs
   a TSV-only action runner vs generated C tables.  See
   [`01-event-system.md`](01-event-system.md).
2. **Hard-mode alternate art:** `*R.bmp` is reserved for a second, harder
   run; decide when to expose that mode.

## Testing strategy

* Save round-trip and checksum validation against all eight original slots.
* Differential state tests: scripted movement/actions vs original under Wine.
* Map rendering tests by tile ID, not pixels.
* Battle-table tests for every monster, including cross/Dragon Slayer.
* Event checklist: floors 1–10 NPCs, boss, fairy/ranking, save0.
* Asset pipeline test: clean source install -> converted data -> playable
  build with no missing files.

## Non-goals for the first milestone

* Audio playback (deferred).
* Full 50-floor content.
* Exact frame-for-frame animation parity.
* Native platform menus or the original Windows font.
