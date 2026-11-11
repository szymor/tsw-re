# C + SDL3 implementation plan

> Status: **Phase 3 core implemented; opening escape event pending** — 640×480
> shell, map rendering, movement/collision, keys/doors, stairs, HUD, inventory,
> pickups/equipment, normal battles, Lua events/dialogs, floors 1–10 NPCs,
> floor-10 boss/fairy, save0 checkpoints, save/load slots, replay mode and
> tests are in place.  The opening escape/cutscene event and its trigger are
> not implemented; that analysis and implementation is **Phase 3.5** below.
> Ranking writes remain gated; floors 11–50 remain in Phase 4.  The authoritative design is
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
  items.h/.c          pickup effects, equipment, special-item use
  battle.h/.c         monster table, battle simulation and forecast
  replay.h/.c         deterministic replay script runner
  events.h/.c         vendored Lua state and event-script loader
  events/*.lua        event/NPC/boss scripts (committed source)
  (later)             text.c, audio.c
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
* Deterministic replay mode: `--replay` supports load/new/move/use/expect/save.
  It resolves the static floor-3/5/9 wall events and rejects unsupported Lua
  events explicitly; dialogue/choice/boss replay is not implemented yet.
* Phase 1 left monsters, non-key items, NPCs and event tiles blocked; Phase 2
  added monsters/items and Phase 3 now handles NPC/event tiles through Lua.

### Phase 2 — Items and combat (implemented)

* Consumables and equipment pickups: implemented with the documented
  floor/difficulty scaling and duplicate protection.
* 15 special-item slots, 3×5 keyboard inventory and item use: implemented.
* Monster table (33 entries), normal encounters and damage formulas:
  implemented, including Cross/Dragon Slayer and Lucky Gold multipliers.
* Monster info: modal Orb of the Hero forecast over distinct monsters on the
  current floor, using effective scaled stats; the original window layout and
  Orb of Wisdom/Flying flows remain polish.
* Death/game over overlay and new-game reset: implemented.
* Special-item effects whose exact original interaction is still uncertain
  (Magic Elixir formula, Warp Staff destination, Orb travel UI) are documented
  as provisional/open questions rather than hidden assumptions.

### Phase 3 — Floors 1–10 events, boss and ranking (core implemented; opening event pending)

* Lua event runtime with coroutine message/choice/tick waits and a small
  state/map API: implemented.
* Old-man first/repeat dialogues on floors 2/3/4/6: implemented from the
  original game and `roujin` (including the 3% floor-2 boost, Orb of the Hero,
  and flags `0x42..0x45`).  Merchant offers are initial approximations.
  Genuine altar interactions are not implemented yet.
* Floor-3 `(4,8)` raw `0x07`: **the earlier wall interpretation is wrong**.
  The tile is the trigger for the opening escape/cutscene sequence, not a
  destructible wall; the player remains in place while the Magic Sergeant
  dialogue and title/cutscene run.  The current `3:92:7` open/teleport handler
  is provisional and must be replaced in Phase 3.5.  The pressable raw-`0x09`
  walls on floors 5 `(8,10)` (beside the Iron Sword) and 9 `(9,4)` are separate
  and remain implemented as `5:118:9` / `9:53:9`.
* Floor-10 Golden Knight → fairy sequence: implemented; the fairy computes a
  provisional area-1 score and writes a zero-checksum `save0.dat` checkpoint.
* Ranking: read-only.  Ranking writes remain disabled until the sixth
  field/footer is decoded.
* Save is rejected during active events; loading/new game cancels suspended
  events.
* Old-man event flags `0x42..0x45` are confirmed from `roujin`.  The floor-10
  completion flag (`0x9c`) and the rest of the original flag map remain
  provisional.

Event implementation: **Lua 5.4 event scripts + TSV static data**.  Lua is
vendored under `third_party/lua`; event scripts live in `src/events/`.  The
event API exposes messages, choices, battles, map/state changes, gold, life,
floor transitions, waits, sound and sprites.  See
`docs/design/01-event-system.md`.  If the original 6-byte VM is re-implemented
for exact animation parity, opcode `0x14`'s state indices need an explicit
mapping onto the proposed `GameState` layout.

### Phase 3.5 — Opening escape event (analysis and implementation)

The opening sequence shown by the original game is not a wall interaction.
It is a story/cutscene event with spawned actors, dialogue, a title/cutscene
transition, and a later floor/position transition.  It must be analysed before
it is implemented again.

#### 3.5A — Original-game analysis (do this before writing gameplay code)

* Resolve the conflicting floor-3 observations before scripting anything: one
  trace treated `(4,8)` raw `0x07` as an immediate open/teleport, while the
  user-reported original encounter at the same tile starts the opening
  cutscene and leaves the player in place.  The plan and event notes must be
  updated from one recorded clean-New-Game call path.
* Trace a **clean original New Game** through the trigger tile and record:
  * exact floor/index/raw trigger tile;
  * the guard flag/index that makes the event one-shot;
  * whether any prerequisite state (new game, floor, equipment) is checked.
* Dump the event timeline from the original handler (currently believed to be
  the `openning2`/`openning31`/`openning9` family; confirm the actual call
  path):
  * message IDs and resolved text;
  * actor spawn slots, sprite IDs, positions, facing, movement, removal;
  * title/cutscene IDs, frame/ticks waits, fades, sounds;
  * when player control is locked/unlocked.
* Record the post-event state:
  * destination floor and x/y;
  * map tile changes;
  * inventory/equipment and stolen-item state;
  * story flags and floor-limit changes;
  * save/load behaviour and whether `save0` changes.
* Store raw captures outside the repository; commit only refined text/data
  once stable.

#### 3.5A trace log (partial)

* Live original trace confirmed that floor-2 raw `0x15` at `(2,6)` is the
  prisoner old-man trigger, **not** an altar.  The player remained at the
  source tile during the dialogue.
* First dialogue captured: “What a relief you're okay! You were caught and
  thrown into this prison by the Magic Sergeant. You were unconscious when you
  got here. Nevertheless, you are so lucky that you just woke up. I've
  finished the escape tunnel. Let's break out.”
* After the first panel, the map marker `0x15` moved from `(2,6)` to `(0,7)`;
  the player remained in place.  Approaching `(0,7)` triggered the second
  dialogue: “O.K. Well, excuse my going first. Your weapon and shield were
  stolen by the guards. It's not wise to wander about without them. I believe
  there is a sword on the 5th floor and a shield on the 9th, you had better get
  them first of all. See you later!”
* After the second panel the marker disappeared.  This confirms the prisoner is
  a **moving event actor encoded on the map**, not a static NPC tile.
* User-confirmed Zeno trigger: entering floor-3 tile `(4,8)`.  The tile starts
  visually as a floor tile and originally holds raw `0x07`; the event changes
  it to `0x06`.  Zeno and four Magic Sergeants appear, the player remains on
  the trigger tile, and the dialogue ends with the player teleported to the
  floor-2 prison.  The player's stats are deliberately nerfed by the capture.
* A post-Orb save was provided at
  `/tmp/opencode/phase35-user/after-orb/Savedat/save1.dat`: floor 3,
  `(x,y)=(10,4)`, life 976, attack/defense 10/10, gold 5, Orb slot 0 = 1,
  and floor-3 tile `(4,8)` already changed `0x07 -> 0x06`.  This confirms the
  post-event map/state baseline; no pre-event save is necessary because the
  launch/new-game trace can reconstruct it.
* Initial Phase 3.5B/C implementation is now in place: actor spawn/move/remove,
  image cutscenes, Zeno’s Magic-Sergeant actors, the exact Zeno/prisoner text,
  the stat nerf, floor transition and the moving prisoner marker.  `open2.bmp`
  is used for the Zeno cutscene.  Actor positions/timing are approximate and
  the one-time launch `open0.bmp` sequence remains to be implemented/traced.

#### 3.5B — Runtime support

Extend the Lua event runtime with the primitives this sequence needs:

* spawn/remove actor, set actor sprite/facing/position, move actor along a
  path, and signal animation/movement completion;
* actor storage, sprite/mask loading, draw ordering, and renderer integration
  (`l_sprite()` is currently a no-op and `Assets` has no actor/title sheet);
* freeze/unfreeze player input during a cutscene, including actor-contact
  routing if the capture shows a persistent/interactable prisoner;
* title/cutscene overlay, fades and timed waits;
* a deterministic fixed 60 Hz event-step contract with a render-loop
  accumulator, replacing the current per-frame `wait(ticks)` approximation;
* explicit floor/position transition at event boundaries, including any
  cross-floor map update/arrival dispatch the capture requires;
* a persistent event-completion flag and save/load cancellation semantics;
* save rejection for the whole event, including cutscene waits;
* shared completion/cancellation/error cleanup for actors, title/fade state,
  pending actor movement and input locks.

Do not add these as ad-hoc game-state hacks; they belong in the event runtime
and must be covered by tests.

#### 3.5C — Implementation

* Replace the provisional floor-3 `3:92:7` handler with the opening event.
* Remove the replay shortcut in `src/replay.c` and update its regression:
  until event-aware replay exists, reject the opening event explicitly instead
  of clearing a tile and teleporting.
* Update the input regression that currently expects the provisional immediate
  teleport.
* Spawn the Magic Sergeants and the prisoner old man with the captured actor
  data.
* Play the Zeno dialogue, cutscene/title sequence, and prisoner dialogue in the
  captured order.
* Apply the observed destination/state transition.
* Keep the ordinary floor-2 old man separate from the event-spawned prisoner.

#### 3.5D — Verification

* Regression: the trigger does not move the player before/during dialogue.
* Regression: chained tick/animation waits are deterministic under different
  render/update cadences (fixed 60 Hz event steps).
* Regression: actor positions/sprites and message order match the capture.
* Regression: the event runs exactly once and the guard flag survives save/load.
* Regression: loading/cancelling/new-game from every new wait kind cleans up
  actors, title/fade state, pending paths and input locks; define whether
  mid-event load is API-only or supported from the keyboard.
* Regression: replay rejects the opening event explicitly until event-aware
  replay is implemented.
* Regression: the post-event floor/position/inventory match the original.
* Differential state/map comparison with the original at event boundaries.

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
| Opening cutscene/actor data | do not implement from screenshots alone; complete the Phase 3.5A trace and add the actor/cutscene primitives before content work |
| Wrong wall interpretation | the floor-3 `3:92:7` handler is provisional; replace it with the analysed opening event in Phase 3.5C |
| Event timing determinism | add a fixed 60 Hz event-step accumulator and animation-completion waits in Phase 3.5B; test chained waits |
| Replay path | remove the floor-3 replay shortcut or reject the opening event until event-aware replay exists |
| Actor/presentation assets | add actor-sheet/title resource loading and draw ordering to Phase 3.5B; keep only visual/audio polish in Phase 5 |

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
14. **Next step:** Phase 3.5 opening escape event: complete the original trace,
    add actor/cutscene runtime support, implement the sequence and verify it.
    Then continue with Phase 4.

## Remaining design questions

* When to expose the `*R.bmp` hard-mode/second-run art.
