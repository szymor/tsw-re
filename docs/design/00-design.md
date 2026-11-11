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
playfield unscaled.  The 800×500 asset variants are not used; they are
upscaled and slightly blurred.  Prefer the non-`R`/non-`b` assets:

* Use the 640×400 set in `Data/`: `Left.bmp`, `Right.bmp`, `CENTER.BMP`,
  `title05.bmp`, `Chtable.oz`, `Key.oz`, `open*.bmp`.
* Do **not** use the 800×500/large-atlas set in `Data3/` (its `.oz` atlases
  are much larger and upscaled).
* The `*R.bmp` files in `Data/` are the same 125×400/390×400 dimensions;
  compare their palettes if needed, but they are not the 800×500 set.

Proposed layout:

```
640×480 logical canvas
+--------------------------------------------------------------+
| custom menu bar: Game  Options  Help                 20 px    |
+----------------+-----------------------------+---------------+
| left panel     | center 390 px: 352×352 map   | right panel   |
| 125 px         | centered (19 px margins)     | 125 px        |
|                |                             |               |
| 400 px tall (original playfield, unscaled)                   |
+----------------+-----------------------------+---------------+
| bottom console: messages / combat log / item details  60 px  |
+--------------------------------------------------------------+
```

Total: `20 + 400 + 60 = 480`.

* The original left/center/right art is drawn at `y=20` without scaling.
* The custom menu bar avoids a platform-native menu and keeps the layout
  portable.
* The extra 80 px is used for the 20 px menu bar and a 60 px console.
  The console can show three lines of text, combat results, or item details.
* Alternative if the menu is an overlay instead of permanent: 400 px
  playfield + 80 px console.  This is a presentation choice; the engine
  supports both.
* Window is resizable; the canvas scales by integer factors (1×, 2×, 3×)
  with letterboxing on non-4:3 displays.  No smooth/fractional scaling by
  default.

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
* Preferred target formats: PNG for graphics, OGG for audio later, TSV/JSON
  for generated tables, plain text for messages.
* The `.oz` tile/key atlases need a decoder (or the user's pre-extracted
  PNGs); the BMP assets can be converted immediately.
* Use the 640×400 sprite set only.  Do not use the 800×500 upscaled variants.

## Runtime asset lookup

1. `--data-dir` command-line option.
2. `data/` next to the executable (installed build).
3. `data/` in the source/build tree (development).
4. A clear error if required assets are missing.

Saves are user data, not assets.  Default to an XDG data directory, with
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

1. **Menu/console layout:** 20 px menu + 400 px playfield + 60 px console
   (recommended) vs 400 px playfield + 80 px console with an overlay menu.
2. **Font:** free metrically similar font (recommended) vs user-supplied
   font.
3. **Event data format:** TSV/JSON loaded at runtime vs generated C tables.
   Recommendation: TSV for events/maps, generated C for small fixed tables.
4. **Save directory:** XDG data dir by default, or a `Savedat/` folder next
   to the executable.
5. **`.oz` decoding:** implement the decoder early, or ship pre-extracted
   PNGs generated from the user's copy.
6. **Text extraction:** robust DFM string-list parser; commit the original
   text under `text/` as-is.

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
