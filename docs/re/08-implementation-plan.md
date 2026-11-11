# C + SDL3 implementation plan

> Status: **design draft** — no PoC code has been written.  The authoritative
> design is [`docs/design/00-design.md`](../design/00-design.md).  This file
> keeps the phase plan and the mapping to the reverse-engineering notes.

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
  in `data/` and are not committed; the final build includes them.  Original
  text is stored as-is under `text/` by project decision.
* Linux first; CMake keeps later ports straightforward.

## Architecture

```
src/
  main.c              SDL init, window, main loop
  game.h/.c           GameState, floor maps, event flags, save/load
  map.c               map decoding, tile properties, rendering
  player.c            movement, doors, keys, stairs, item pickup
  battle.c            monster table, damage, monster info
  items.c             inventory, equipment, consumables, special items
  events.c            Lua state, event API, coroutine runner
  events/*.lua        event/NPC/boss scripts (committed source)
  ui.c                panels, HUD, keyboard menu overlay, console
  assets.c            asset loading, fallback placeholders
  save.c              original save/option/ranking formats
  text.c              original message table and rendering
  audio.c             deferred BGM/SFX
third_party/lua/      vendored Lua 5.4 (MIT)
tools/
  extract_initial_map.py
  extract_saves.py
  extract_text.py
  extract_sprites.py
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

* Convert the user's original **640×400** assets to PNG/OGG/TSV as needed.
  Do **not** use the 800×500 upscaled variants (`*R.bmp`, `Chtableb.oz`).
* Generated assets live in `data/` and are **not committed**.  CMake
  `install()` copies them into the final build so the distributed build is
  self-contained.
* Conversion scripts live in `docs/re/tools/` (and later a top-level
  `tools/` directory).
* The original game text is committed under `text/` as-is, without
  paraphrasing, by project decision.
* A placeholder mode may remain for development/debugging, but the target
  build uses the converted original assets.

## Implementation phases

### Phase 0 — Skeleton (1–2 days)

* SDL3 window, **640×480 logical canvas**, integer scaling; original
  640×400 playfield at `y=0`.
* Render loop, input mapping, placeholder tile atlas.
* 80 px bottom console; no permanent menu bar.
* Keyboard menu overlay (`F10`) and keyboard-only controls.
* HUD panels (left stats, center map, right equipment/keys, console).
* Vendor Lua 5.4, build it as a static library, and run one trivial event
  script to validate the embedding.
* Debug overlay for state.

### Phase 1 — Core exploration (3–5 days)

* Decode the initial map from a user-provided save or generated data.
* 11×11 map render, player movement, collision, step animation.
* Doors + three key colors, key grid.
* Stairs and floor transitions (including the 43↔45 skip).
* Floor/stat HUD, message box.
* Save/load in the original 6957-byte format, checksums, slots 1..8,
  `save0` checkpoint read support.
* Deterministic test mode: load a save, move a scripted sequence, compare
  state block bytes with the original.

### Phase 2 — Items and combat (3–5 days)

* Consumables (keys, life/attack/defense potions, crystals).
* 15 special items, 5 swords, 5 shields, inventory grid.
* Monster table (33 entries), normal encounters, damage formulas.
* Monster info window and Hero Orb forecast.
* Death/game over reset.
* Generate a local monster/item data file from the user's EXE or use the
  table in `06-data-tables.md`.

### Phase 3 — NPCs, events, bosses (1–2 weeks)

* Event dispatcher for floors 10..49.
* Old men, merchants, altars, thief, wizards, fairies.
* Area bosses on floors 10, 20, 25, 40, 49.
* Secret events (floor 15/35 tunnels; floor 44 illusion; dragon).
* Fairy score, ranking files `option1..5.dat`, `save0.dat` checkpoint.
* Ending images/sequence.

Event implementation: **Lua 5.4 event scripts + TSV static data**.  Lua is
vendored under `third_party/lua`; event scripts live in `src/events/`.  The
event API exposes messages, choices, battles, map/state changes, gold, life,
floor transitions, waits, sound and sprites.  See
`docs/design/01-event-system.md`.  If the original 6-byte VM is re-implemented
for exact animation parity, opcode `0x14`'s state indices need an explicit
mapping onto the proposed `GameState` layout.

### Phase 4 — Polish (3–5 days)

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
| Exact NPC dialog/text | extract from the user's DFM into `text/` and keep it as-is |
| `.oz` atlas decompression | write a decoder or use pre-extracted PNGs generated from the user's copy |
| MIDI playback | deferred; later SDL_mixer + timidity/fluidsynth on Linux |
| Original save compatibility | implement/validate the checksums and magic/version fields first |
| Balance constants | use the extracted tables; expose them in a data file for tuning |
| Timeline control flow | the current extractor is a static append-site inventory; add control-flow/runtime validation before final event scripting |

## Decisions (from the project owner)

1. **Scope:** floors 1–10 first, then the full game.
2. **Assets:** reuse original assets with format conversion; generated
   `data/` is not committed; the final build includes it; scripts are in the
   repository.
3. **Text:** store the original game text in `text/` as-is, no paraphrase.
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
11. **`.oz` conversion:** implement the decoder in the conversion script;
    converted PNGs go to `data/`.
12. **Text:** refine the DFM extraction during implementation; original text
    stays in `text/`.
13. **Events:** Lua 5.4 event scripts + TSV static data; Lua vendored under
    `third_party/lua`; scripts in `src/events/`.
14. **Next step:** design review, then Phase 0–1.

## Remaining design questions

* When to expose the `*R.bmp` hard-mode/second-run art.
