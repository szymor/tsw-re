# C + SDL3 implementation plan

> Status: **Phases 0–3.5 done; Phase 4 in progress.**  The 640×480 shell,
> exploration, items, normal battles, save/load (including `save0`
> checkpoints), replay mode and the launch intro are in place.  Every event
> runs on the C port of the original command VM, driven by Lua builders
> ported from the original handlers and accepted on golden-vector parity
> with the native oracle (see the event table under Phase 3).  What is left
> is listed under Phase 4; the analysis side is
> [`11-event-analysis-plan.md`](11-event-analysis-plan.md).  Ranking writes
> remain disabled.  The authoritative design is
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
* Reuse the original assets with format conversion (see Asset strategy).
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
  game.h/.c           new/load/snapshot (moves run builders.handan)
  items.h/.c          special-item use (still C; Phase 4 item 2)
  battle.h/.c         monster table and the damage forecast
  replay.h/.c         deterministic replay script runner
  events.h/.c         event runtime wrapper (owns the event engine)
  event_vm.h/.c       original 6-byte command VM
  event_builders.h/.c builder runtime (Lua builders emit original commands)
  event_engine.h/.c   Timer2 tick, dialogue/choice suspension, presentation
  messages.h/.c       ListBox2 dialogue text (docs/re/18-messages.md)
  intro.h/.c          open0.bmp launch intro timing
  events/builders.lua original-handler builders (committed source)
  (later)             text.c, audio.c
third_party/lua/      vendored Lua 5.4 (MIT)
tools/
  build_sdl3_headless.sh  SDL3 libraries into a private prefix
  convert_assets.py   BMP/.oz -> data/gfx PNGs
  extract_messages.py ListBox2 text -> data/text/messages.tsv
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

### Phase 0 — Skeleton (done)

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

### Phase 1 — Core exploration (done)

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
* Deterministic replay mode: `--replay` supports load/new/move/use/expect/
  save and the window answers (`choose`, `altar`, `tryagain`).  It runs the
  event engine headless, so moves, battles and events behave as in play.
* Phase 1 left monsters, non-key items, NPCs and event tiles blocked; Phase 2
  added monsters/items and Phase 3 handles NPC/event tiles through builders.

### Phase 2 — Items and combat (done)

* Consumables and equipment pickups: implemented with the documented
  floor/difficulty scaling and duplicate protection.
* 15 special-item slots, 3×5 keyboard inventory and item use: implemented.
* Monster table (33 entries), normal encounters and damage formulas:
  implemented, including Cross/Dragon Slayer and Lucky Gold multipliers.
* Item use runs the original's handlers (Phase 4 below); the Orb of the
  Hero's window shows `img6work`'s rows (at most seven kinds, map order,
  `obudameiji`'s damage); the original window layout and the Orb of
  Wisdom's window remain polish.
* Death/game over overlay and new-game reset: implemented.

### Phase 3 — Event runtime, floors 1–10 and the opening (done)

Events run on the original 6-byte command VM re-implemented in C
(`event_vm.c`), driven by **Lua 5.4 builder scripts** (`src/events/
builders.lua`) that emit the original commands, plus a `timer2`-equivalent
tick, dialogue/choice suspension and presentation hooks (`event_engine.c`).
Builders never yield.  Opcode `0x14` state indices are the `GameState`
indices directly (`12-event-vm.md`).  Paths that are not ported call
`fail()`; the engine restores the state and logs the failure.  An unported
`0x0b` splice is logged, skipped and counted (`EventEngine.unported`).  See
`docs/design/01-event-system.md` §7.

Engine rules that hold for every event: input is locked while the command
count is non-zero; save is rejected during an event; load/new game clears the
buffer, the auxiliaries and all presentation state.

The opening (Phase 3.5) is complete: the floor-3 `(4,8)` raw `0x07` trigger
runs `handan` → `mevent` → `zenolivework`, the Zeno dialogue continues into
`openning2`/`openning31`/`openning9` (the capture and the demotion to floor 2,
`17-static-findings.md` §1), and the prisoner is `touzoku`.  The one-time
`open0.bmp` launch intro is `src/intro.c`.  Details and witnesses are in
[`16-event-pilot.md`](16-event-pilot.md).

#### Ported events

A ported event passes builder parity (and, where listed, drain parity)
against locally exported golden vectors; nothing else counts.

| Event | Original handler | Notes |
|---|---|---|
| old men (raw `0x19`/`0x1a`) | `handan` → `roujin` | text from `ListBox2`; confirm is `Button1Click` case 0 |
| dialogue confirms | `Button1Click` | 25/28 blocks covered (`16-event-pilot.md` §9) |
| floor-3 opening | `mevent`, `idou`, `zenolivework`, `openning2`, `madoushilivework`, `openning31` | builder and drain parity |
| thief / floor-2 prisoner (raw `0x15`/`0x16`) | `touzoku`, `doorstackin`, `soundcheck` | builder and drain parity |
| secret walls (raw `0x09`, floors 5 and 9) | `handan` case 9 | door kind 7, status text `0x89`; the player does not move |
| merchants (raw `0x13`/`0x14`) | `syounin`, `Button2Click`/`Button7Click` | Yes/No question |
| altars (raw `0x10`) | `handan` case `0x10`, `Button2Click` mode 1, `Button39..42Click` | price `10*(n²-n+2)` |
| fairy (raw `0x17`/`0x18`) | `yousei` | score, temporary ranking, `save0.dat`, sub-event 26 sets `0x6a` |
| `ichicheck`, all 14 floor cases | `ichicheck` via `mevent`/`handan` raw `0x07` | 69/69 blocks (`16-event-pilot.md` §12) |
| movement attacks | `idou` checks: `mazyutu1`, `madoushi1` | also after every ordinary C step (`event_engine_after_step`); `16-event-pilot.md` §12b |
| every `handan` raw | doors, keys, items, equipment, stairs, battles (`taisen`), passages, Zeno's cells, the floor-50 ending | `16-event-pilot.md` §12c; all `action_*` vectors |
| post-battle `moncheck` | `moncheck`, all 22 floor cases | 85/85 blocks, 152/152 edges, 642/642 append sites (`16-event-pilot.md` §12a) |
| event battles | `taisen2` (sub-dispatch 10) | both branches drain; a loss runs the death window, the "try again?" `MessageDlg` and op `0x16` → `gameover` |
| floor-49 Magic Sergeants | `madoushi2` (sub-dispatch 3) | builder and drain parity; floor 49 drains end to end |
| floor-20 vampire | `banpaiawork` (sub-dispatch 4) | presentation only (no state/map effect); floor 20 drains end to end; drawn as the 3x3 bat/vampire block |

Walkable tiles follow `handan`: only `0x06` walks; `0x04`/`0x05` (door
frames), `0x0f` and `0x11` block.  `0x0e` stays walkable until the Snow
Crystal is ported (known divergence, `09-open-questions.md` item 14c).
Floor flags: `0x62` (floor-2 old man) and `0x6a` (area-1 completion) are
confirmed; the rest of the flag map is in `15-event-flags.md`.

### Phase 4 — Remaining game (in progress)

Floors 11–50, remaining NPCs, bosses and endings, ported handler by handler
as Lua builders in the order the analysis exports golden vectors.  A floor is
done when its builders pass vector parity **and** every splice its timelines
queue has a builder; a placeholder script does not count.  Open items, in
priority order:

1. **Splices queued by already ported floors:** a drain witness for
   `openning9` (the losing `taisen2` branch, the death window and op `0x16`
   → `gameover` are ported; sub-dispatch 6 belongs to the ending).
2. **Item use as the original's handlers (done).**  Enter on the
   inventory runs the slot's handler (`img6work`..`img17work`, as
   `formkeydown` does on the item overlay); the Yes/No panel runs
   `Button38Click`/`Button37Click` and the Orb of Flying's panel
   `Button8/9/10Click`, all builders with golden vectors
   (`16-event-pilot.md` §12d).  `item_use_special` is gone.  The remake
   keeps its own 3×5 inventory instead of the overlay cursor
   (`itemichiwork`).
3. **Presentation of the handan timelines** now that play uses them: the
   battle hit (op `0x0e`) and the stairs' `0x15` wipe are logged, not drawn
   (below); the status line (`status_text`/`status_concat`) is the
   console's only battle/pickup text.
4. Later area bosses and endings; full ranking/checkpoint behaviour for all
   areas (ranking writes need the sixth field/footer, `09-open-questions.md`).

Presentation still short of the original, found in play-testing (state and
command order already match):

* the opening's map → `open1.bmp` dissolve (`openning9` phase 0, frames
  `0xe8..0xf7`, mask tiles `0xa6..0xb5` with sound 4 each) is shown as an
  instant cut;
* op `0x0e` (battle hit) and op `0x15` (draw actor) are logged, not drawn;
* audio is not implemented (sound 4 is the hit sound the opening relies on).

After behaviour changes that the execution oracle also models (for example
the ranking form's OnClose restarting Timer2), re-export the golden vectors
so the drain parity covers the new behaviour.

### Phase 5 — Polish and packaging

* Keyboard menu overlay (Game/Options/Help), save/load/ranking dialogs.
* Options file (controls/display/sound) and internal option/carry flags.
  The original has **no user-facing difficulty switch**.
* Audio (MIDI/BGM) deferred until mechanics are complete.
* Original-asset mode validation, palette/transparency fixes.
* Cross-platform build (Linux first; SDL3 keeps Windows/macOS easy).

## Testing strategy

* **Event golden vectors**: for each ported builder, compare command words
  and state/map/aux effects with the native oracle on identical synthetic
  snapshots, and the drained result where the vector has a drain.  Vectors
  are exported locally (`docs/re/tools/export_event_vectors.py`) and are not
  committed; the tests are skipped when they are absent.
* **Coverage gate**: `docs/re/tools/check_event_coverage.py --require
  <handler>` checks executed blocks, edges and append sites against the
  static CFG (`16-event-pilot.md` §9).
* **Ordinary actions** are `handan` vectors like any other event: the
  `action_*` witnesses (doors with/without keys, keys, every item raw
  `0x20..0x3c` incl. area 2, option A and owned equipment, stairs incl. the
  floor limit and floor 0, monster fights incl. a hopeless one) run through
  `builders.handan` in `test_event_builders` with command, state, map, aux
  and drain parity.  (`test_game_parity`, which checked the old C movement
  code against them, is gone with that code.)
* **Event regressions**: a one-shot event does not retrigger after save/load;
  the command sequence is identical under different render/update cadences;
  load/new game at any point of a timeline clears the buffer, actors,
  title/fade state and input locks; `test_replay` plays the floor-3
  opening and a battle through the headless engine.
* **Map rendering golden tests**: render each floor to a PNG and compare
  tile IDs, not pixels (pixel comparison only for the final art pass).
* **Battle table tests**: for each monster, check damage dealt/taken and
  victory/gold against the formulas.
* **Save round-trip**: read/write each original save and compare bytes
  (excluding the timestamps/metadata bytes if they are volatile).

## Risks / unknowns

| Risk | Mitigation |
|---|---|
| Unported event scripts on floors 11–50 | port from golden vectors in game order; the coverage gate shows what is left |
| Splices queued by ported handlers but not ported | count them (`EventEngine.unported`); a floor is not done while its timelines hit one |
| MIDI playback | deferred; later SDL_mixer + timidity/fluidsynth on Linux |
| Balance constants | use the extracted tables; expose them in a data file for tuning |
| Placeholder events mistaken for specification | only ported builders exist; keep the Phase 3 event table current |
| Replay diverging from the engine | resolved: replay runs the same engine headless |
| Item use diverging from the original | resolved: the item handlers are builders with golden vectors |

## Decisions (from the project owner)

1. **Scope:** floors 1–10 first, then the full game.
2. **Assets:** reuse original assets with format conversion; generated
   `data/` is not committed; the final build includes it; scripts are in the
   repository.  `Data/` is the 640×400 set; `Data3/` (800×500) is not used.
   `*R.bmp` is alternate art for a second, harder run and is not used in the
   first slice.
3. **Text:** raw extraction stays outside the repository
   (`$XDG_CACHE_HOME/tsw/`); refined text is committed under `text/` once the
   format is stable.
4. **Saves:** full original `saveN.dat` compatibility.
5. **Platform:** Linux first; C11 + SDL3 + CMake.
6. **Audio:** deferred until mechanics are complete.
7. **Layout/input:** 640×480 = 400 px playfield + 80 px console; no permanent
   menu bar; fully keyboard-controllable, menus as keyboard overlays.
8. **Font:** Liberation Sans, fallback DejaVu Sans, then Verdana; no bundled
   font in the first build.
9. **Save directory:** `$XDG_DATA_HOME/tsw` or `~/.local/share/tsw`
   (no `-re` suffix), with `--save-dir` override.
10. **`.oz` conversion:** decode the 8-bit BMP containers in the conversion
    script; converted PNGs go to `data/`.
11. **Events:** original command VM in C + Lua 5.4 builder scripts; Lua
    vendored under `third_party/lua`; scripts in `src/events/`.  Supersedes
    the coroutine-only decision; see `docs/design/01-event-system.md` §7.

## Remaining design questions

* When to expose the `*R.bmp` hard-mode/second-run art.
