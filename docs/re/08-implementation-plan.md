# C + SDL3 implementation plan

> Status: **Phase 1 core exploration implemented** — 640×480 shell, map
> rendering, movement/collision, keys/doors, stairs, HUD, save/load slots,
> save parsing, Lua embedding, replay mode and tests are in place.  Combat,
> items and events remain in later phases.  The authoritative design is
> [`docs/design/00-design.md`](../design/00-design.md).  This file keeps the
> phase plan and the mapping to the reverse-engineering notes.

## Goals

* Recreate the **gameplay experience** of Tower of the Sorcerer v1.2:
  exploration, keys/doors, items, combat, NPCs, bosses, saves and rankings.
* Use **C11 + SDL3**; SDL_ttf for text; SDL_mixer or a small MIDI path for
  audio later.
* Use a **640×480 logical canvas**: the original 640×400 playfield stays
  unscaled, with an 80 px bottom console and **no permanent menu bar**.
  The game is fully keyboard-controllable; menus are keyboard overlays.
  The 800×500 upscaled asset variants are not used.
* Reuse the original assets with format conversion.  Converted assets live
  in `data/` and are not committed; the final build includes them.  Raw
  extracted text stays outside the repository (`$XDG_CACHE_HOME/tsw/`);
  refined text may be committed under `text/` later.
* Linux first; CMake keeps later ports straightforward.

## Architecture

```
src/
  main.c              CLI, startup, screenshot/inspection modes
  app.h/.c            application state and main loop
  state.h/.c          684-byte state-block parsing/checksums
  map.h/.c            6273-byte map block, floor/tile lookup
  save.h/.c           original save load/validation
  assets.h/.c         PNG/BMP/atlas + font loading
  render.h/.c         640x480 canvas, panels, map, console, menu
  input.h/.c          keyboard mapping, menus and movement actions
  ui.h/.c             F10 menu/submenu/confirmation state
  log.h/.c            bottom console log buffer
  game.h/.c           new/load/snapshot, movement, keys/doors/stairs
  replay.h/.c         deterministic replay script runner
  events.h/.c         vendored Lua state and event-script loader
  events/*.lua        event/NPC/boss scripts (committed source)
  (later)             battle.c, items.c, text.c, audio.c
third_party/lua/      vendored Lua 5.4 (MIT)
tools/
  convert_assets.py   BMP/.oz -> data/gfx PNGs
  extract_maps.py     initial map -> data/maps/initial.bin
                      save debug copy -> data/maps/save-map.bin
docs/re/tools/        analysis-side extraction/readers
data/                 generated locally from the user's copy (gitignored)
```

The game state should mirror the original state block so that original save
files can be imported/exported:

```c
typedef struct {
    int32_t life, attack, defense, gold;
    int32_t floor, floor_limit, x, y;
    int32_t keys[3];          // yellow, red, blue
    int32_t weapon_level, shield_level;
    int32_t special[15];      // 0x4b86cc .. 0x4b8704
    int32_t weapon_flags[5];
    int32_t shield_flags[5];
    int32_t events[...];      // 0x4b8730 .. 0x4b88fb
    int32_t options[2];       // 0x4b8904, 0x4b8908
    int32_t magic, version;   // -4, 12
} GameState;
```

The runtime map is `uint8_t maps[51][121]` plus the 50 two-byte trailers,
or a flat 6271-byte array with the same stride as the original.

## Asset strategy

* Convert the user's original **640×400** `Data/` assets to PNG/OGG/TSV as
  needed.  `Chtableb.oz` is the sprite bitmask for `Chtable.oz` and is
  required.  Do **not** use the `Data3/` 800×500 set.  `*R.bmp` is alternate
  art for a later hard-mode/second run, not an upscaled variant.
* Generated assets live in `data/` and are **not committed**.  CMake
  `install()` copies `data/` into the final build so the distributed build is
  self-contained.
* Build-time conversion scripts live in `tools/`; analysis-side readers remain
  in `docs/re/tools/`.
* Raw extracted text is generated outside the repository, by default into
  `$XDG_CACHE_HOME/tsw/` (or `~/.cache/tsw/`), and is not committed.  Refined
  text may be committed under `text/` later when the message format is stable.
* A placeholder mode may remain for development/debugging, but the target
  build uses the converted original assets.

## Implementation phases

### Phase 0 — Skeleton (Part 1 shell done)

* SDL3 window, **640×480 logical canvas**, integer scaling; original
  640×400 playfield at `y=0`.
* Render loop, input mapping, original tile atlas.
* 80 px bottom console; no permanent menu bar.
* Keyboard menu overlay (`F10`) and keyboard-only controls.
* Panel backgrounds, center map, right-side equipment panel, console and live
  HUD stat/equipment/key text.
* Vendor Lua 5.4, build it as a static library, and run one trivial event
  script to validate the embedding.
* State debug overlay: deferred; the HUD covers the core state fields.

### Phase 1 — Core exploration (implemented)

* Initial map block and 11×11 rendering: done in Part 1.
* Player movement and collision: implemented.
* Player sprite and facing: implemented using the original Chtable
  color/mask pairs (down, left, right, up); a short 90 ms step slide is
  used for movement.  Further animation polish remains optional.
* Doors + three key colors, key grid: implemented; opened doors become raw
  `0x06`, matching saved original maps.
* Stairs and floor transitions (including the 43↔45 skip): implemented.
* Floor/stat HUD and console messages: implemented.
* Save/load in the original 6957-byte format, checksums, slots 1..8,
  `save0` checkpoint read support: implemented.  XDG save directory and
  `--save-dir` override are supported.
* Deterministic replay mode: `--replay` supports load/new/move/expect/save.
* Monsters, non-key items, NPCs and event tiles are intentionally blocked
  until Phase 2/3; the console reports why.

### Phase 2 — Items and combat (3–5 days)

* Consumables (keys, life/attack/defense potions, crystals).
* 15 special items, 5 swords, 5 shields, inventory grid.
* Monster table (33 entries), normal encounters, damage formulas.
* Monster info window and Hero Orb forecast.
* Death/game over reset.
* Generate a local monster/item data file from the user's EXE or use the
  table in `06-data-tables.md`.

### Phase 3 — Floors 1–10 events, boss and ranking (1–2 weeks)

* Event dispatcher and Lua scripts for floors 1–10.
* Old men, merchants, altars, and the floor-10 area boss/fairy.
* Area-1 fairy score, ranking update, `save0.dat` checkpoint.
* Prerequisite: resolve the area-1 scoring/ranking update rules
  (sixth field/footer); otherwise ranking is read-only for the slice.
* Save only at quiescent boundaries; loading cancels suspended events.

Event implementation: **Lua 5.4 event scripts + TSV static data**.  Lua is
vendored under `third_party/lua`; event scripts live in `src/events/`.  The
event API exposes messages, choices, battles, map/state changes, gold, life,
floor transitions, waits, sound and sprites.  See
`docs/design/01-event-system.md`.  If the original 6-byte VM is re-implemented
for exact animation parity, opcode `0x14`'s state indices need an explicit
mapping onto the proposed `GameState` layout.

### Phase 4 — Remaining game (post-slice)

* Floors 11–50, remaining NPCs/bosses/secret events.
* Later area bosses (floors 20, 25, 40, 49) and endings.
* Full ranking/checkpoint behavior for all areas.

### Phase 5 — Polish and packaging

* Keyboard menu overlay (Game/Options/Help), save/load/ranking dialogs.
* Options file (controls/display/sound) and internal option/carry flags.
  The original has **no user-facing difficulty switch**.
* Audio (MIDI/BGM) deferred until mechanics are complete.
* Original-asset mode validation, palette/transparency fixes.
* Cross-platform build (Linux first; SDL3 keeps Windows/macOS easy).

## Testing strategy

* **State-by-state differential tests**: load each `saveN.dat`, perform a
  scripted action, compare the 684-byte state block and 6273-byte map block
  with the original game (run under Wine).
* **Map rendering golden tests**: render each floor to a PNG and compare
  tile IDs, not pixels (pixel comparison only for the final art pass).
* **Battle table tests**: for each monster, check damage dealt/taken and
  victory/gold against the formulas.
* **Save round-trip**: read/write each original save and compare bytes
  (excluding the timestamps/metadata bytes if they are volatile).
* **Event coverage**: a checklist of all 50 floors, NPCs, bosses, items.

## Risks / unknowns

| Risk | Mitigation |
|---|---|
| Unknown `0x61..0x9d` event scripts | use the decoded `ichicheck` table and timeline inventory; build a data-driven event table |
| Exact NPC dialog/text | extract raw text outside the repository (`$XDG_CACHE_HOME/tsw/`); commit refined text under `text/` when stable |
| `.oz` atlases | resolved: they are 8-bit BMP containers; `tools/convert_assets.py` decodes them with Pillow |
| MIDI playback | deferred; later SDL_mixer + timidity/fluidsynth on Linux |
| Original save compatibility | implement/validate the checksums and magic/version fields first |
| Balance constants | use the extracted tables; expose them in a data file for tuning |
| Timeline control flow | the current extractor is a static append-site inventory; add control-flow/runtime validation before final event scripting |

## Decisions (from the project owner)

1. **Scope:** floors 1–10 first, then the full game.
2. **Assets:** reuse original assets with format conversion; generated
   `data/` is not committed; the final build includes it; scripts are in the
   repository.
3. **Text:** raw extraction stays outside the repository
   (`$XDG_CACHE_HOME/tsw/`); refined text is committed under `text/` once the
   format is stable.
4. **Saves:** full original `saveN.dat` compatibility.
5. **Art:** reuse all assets as-is plus conversion.  `Data/` is the 640×400
   set; `Data3/` is the 800×500 set.  `*R.bmp` is alternate art for a
   second, harder run and is not used in the first slice.
6. **Platform:** Linux first; C11 + SDL3 + CMake.
7. **Audio:** deferred until mechanics are complete.
8. **Layout/input:** 640×480 = 400 px playfield + 80 px console; no permanent
   menu bar; fully keyboard-controllable, menus as keyboard overlays.
9. **Font:** Liberation Sans, fallback DejaVu Sans, then Verdana; no bundled
   font in the first build.
10. **Save directory:** `$XDG_DATA_HOME/tsw` or `~/.local/share/tsw`
    (no `-re` suffix), with `--save-dir` override.
11. **`.oz` conversion:** decode the 8-bit BMP containers in the conversion
    script; converted PNGs go to `data/`.
12. **Text:** raw extraction stays outside the repository
    (`$XDG_CACHE_HOME/tsw/`); refine the DFM extraction during implementation
    and commit the refined `text/` file when its format is stable.
13. **Events:** Lua 5.4 event scripts + TSV static data; Lua vendored under
    `third_party/lua`; scripts in `src/events/`.
14. **Next step:** Phase 2 items and combat.

## Remaining design questions

* When to expose the `*R.bmp` hard-mode/second-run art.
