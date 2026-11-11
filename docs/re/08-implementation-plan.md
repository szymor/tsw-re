# Proposed C + SDL3 implementation plan

> Status: **proposal only** — no PoC code has been written.  This plan is
> waiting for the user's approval and answers to the questions at the end.

## Goals

* Recreate the **gameplay experience** of Tower of the Sorcerer v1.2:
  exploration, keys/doors, items, combat, NPCs, bosses, saves and rankings.
* Use **C11 + SDL3**; SDL_ttf for text; SDL_mixer or a small MIDI path for
  audio (optional in the first milestone).
* Prefer functional equivalence over pixel-perfect UI.  A clean 640×400-style
  layout is enough; exact fonts/animations can come later.
* Never ship copyrighted game assets.  Load the user's own original files at
  runtime when available; otherwise use placeholder/procedural art and
  paraphrased text.

## Architecture

```
src/
  main.c              SDL init, window, main loop
  game.h/.c           GameState, floor maps, event flags, save/load
  map.c               map decoding, tile properties, rendering
  player.c            movement, doors, keys, stairs, item pickup
  battle.c            monster table, damage, monster info
  items.c             inventory, equipment, consumables, special items
  events.c            NPCs, altars, merchants, bosses, fairy/ranking
  ui.c                panels, HUD, menus, message box, key grid
  assets.c            asset loading, fallback placeholders
  save.c              original save/option/ranking formats
  text.c              message table, original/paraphrased modes
  audio.c             optional BGM/SFX
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

1. **Original-assets mode** (preferred for the user):
   * Detect the user's installation directory.
   * Load `Data/Left.bmp`, `Right.bmp`, `CENTER.BMP`, `Chtable.png`/`.oz`,
     `Key.oz`, `open*.bmp`, `Midi/*.mid`.
   * Load the DFM text list from the user's `TSW.exe` (via the extraction
     tool) or from a generated local `data/text.txt`.
2. **Placeholder mode** (default if assets are missing):
   * Colored 32×32 tiles with simple glyphs for doors/keys/items/monsters.
   * A small bitmap font or SDL_ttf with a permissive font.
   * Paraphrased/short messages so no copyrighted text is required.
3. Generated `data/` is `.gitignore`d.  The repository contains only code and
   analysis.

## Implementation phases

### Phase 0 — Skeleton (1–2 days)

* SDL3 window, 640×400 logical canvas, integer scaling.
* Render loop, input mapping, placeholder tile atlas.
* HUD panels (left stats, center map, right equipment/keys, bottom message).
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

### Phase 4 — Polish (3–5 days)

* Menus (Game/Options/Help), save/load/ranking dialogs.
* Options file and difficulty/carry flags.
* Audio (MIDI/BGM, optional).
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
| Unknown `0x61..0x9d` event scripts | analyze `handan`'s else branch and `ichicheck` jump table; build a data-driven event table |
| Exact NPC dialog/text | extract locally from the user's DFM; paraphrase in placeholder mode |
| `.oz` atlas decompression | use the user's extracted `Chtable.png`/BMPs, or write an `.oz` decoder if needed |
| MIDI playback | make audio optional; SDL_mixer + timidity/fluidsynth on Linux |
| Original save compatibility | implement/validate the checksums and magic/version fields first |
| Balance constants | use the extracted tables; expose them in a data file for tuning |

## Questions for approval

1. **Scope for the first PoC**: full 50-floor game, or a vertical slice
   (floors 1–10, including the Skeleton boss and ranking) first?
2. **Assets**: should the PoC load your original installation assets, start
   with procedural placeholders, or support both (recommended: both)?
3. **Text**: load the original English messages from your copy, or use
   paraphrased placeholder text in the repository?
4. **Save compatibility**: must the PoC read/write the original `saveN.dat`
   format exactly (recommended), or is a new format acceptable?
5. **UI fidelity**: functional 640×400 layout with new art, or a closer
   reproduction of the original panel sizes/fonts?
6. **Platform**: Linux desktop only for the PoC, or cross-platform from day
   one?
7. **Audio**: include MIDI/BGM in the first milestone, or defer it?
8. **Approval**: do you want me to start Phase 0–1 now, or produce a more
   detailed event-by-event design first?
