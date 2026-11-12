# C + SDL3 implementation plan

> Status: **Phase 3 implemented; Phase 3.5 nearly done** — 640×480
> shell, map rendering, movement/collision, keys/doors, stairs, HUD, inventory,
> pickups/equipment, normal battles, Lua events/dialogs, floors 1–10 NPCs,
> floor-10 boss/fairy, save0 checkpoints, save/load slots, replay mode and
> tests are in place.  Phase 3.5B is done (C command VM, Timer2 tick, Lua
> builder runtime, event engine); the floor-3 opening trigger runs on
> builders with golden-vector parity, and the whole opening (Zeno, the
> capture cutscene, the prison wake-up) is ported and matches the native
> execution oracle end to end (drain parity).  Merchants, altars, secret
> walls, the floor-2 prisoner, floor 10 (boss room, `moncheck`, fairy) and the
> old men are ported too.  The `open0.bmp` launch intro and event-driven
> battles (`taisen2`) are ported; Phase 3.5 is complete.
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
  event_vm.h/.c       original 6-byte command VM
  event_builders.h/.c builder runtime (Lua builders emit original commands)
  event_engine.h/.c   Timer2 tick, dialogue/choice suspension, presentation
  messages.h/.c       ListBox2 dialogue text (docs/re/18-messages.md)
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

### Phase 3 — Floors 1–10 events, boss and ranking (implemented; ranking writes gated)

* Lua event runtime with coroutine message/choice/tick waits and a small
  state/map API: implemented first and since **removed**
  together with `src/events/events.lua`; `events.c` is engine-only.
* Old-man first/repeat dialogues on floors 2/3/4/6: implemented from the
  original game and `roujin` (including the 3% floor-2 boost, Orb of the Hero,
  and the confirmed floor-2 flag `0x62`; floor-3/4/6 flags remain
  provisional).  Merchant offers are initial approximations.
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
* Old-man floor-2 flag is `0x62` (`0x4b8810`); the floor-10 area-1
  completion flag is `0x6a` (`0x4b8830`).  The remaining `roujin` flags
  (`0x43..0x45`) are provisional because those numbers are also
  `DAT_0048c55c` selector values; the rest of the original flag map remains
  provisional.  See `15-event-flags.md`.

Event implementation (**revised**): the original 6-byte command VM
re-implemented in C, driven by **Lua 5.4 builder scripts** that emit the
original commands, plus TSV static data.  Lua is vendored under
`third_party/lua`; scripts live in `src/events/`.  The earlier coroutine API
(messages, choices, battles, waits, actors) served the placeholder events
below and has been removed.  Opcode `0x14` state indices are the
`GameState` indices directly (`12-event-vm.md`), so no remapping layer is
needed.  See `docs/design/01-event-system.md` §7.

#### Provenance of the current Lua events

Every event was once in `src/events/events.lua` (now removed) and was either
checked against the original or a placeholder.  A placeholder must not be treated as a specification.

| Event key | Status | Notes |
|---|---|---|
| old men (raw `0x19`/`0x1a`, every `roujin` floor) | **ported** | `handan` → `roujin` builder (parity: `roujin_*`, `handan_roujin_*`); text from `ListBox2`; confirm = `button1click` case 0 (parity: `button1click_00`) |
| dialogue confirms (`Button1Click`) | **ported** except case 20, the `0x1a` difficulty-0 branch and id 9 | parity on 12 `button1click_*` witnesses |
| floor 3 raw `0x07` (Zeno trigger) | **ported** | `handan` → `mevent` → `idou`/`zenolivework` builders on the command VM; parity with the `handan_floor3*` vectors |
| Zeno's speech and the capture (sub-events 20, 21, 11–13) | **ported** | `openning2`, `madoushilivework`, `openning31`, `Button1Click`; golden-vector and drain parity (`handan_floor3*`, `openning2`, `openning31*`, `button1click_14`) |
| raw `0x15`/`0x16` (thief; floor-2 prisoner) | **ported** | `handan` → `touzoku` builder with `doorstackin`/`soundcheck`; builder and drain parity on 15 `touzoku_*`/`handan_touzoku_*` witnesses; the legacy `2:68:21`/`2:77:21` scripts are gone |
| raw `0x09` (secret walls, floors 5 and 9) | **ported** | `handan` case 9 builder (door kind 7, status text `0x89`); parity on `handan_secret_wall_*`; the legacy scripts (which also moved the player) are gone |
| raw `0x13`/`0x14` (merchants, every `syounin` floor) | **ported** | `syounin` + `Button2Click` (Yes) / `Button7Click` (No) builders; Yes/No question in the engine; parity on 23 `syounin_*`/`button2click_*`/`button7click_*` witnesses; the approximate placeholders are gone.  The altar menu (`choice_mode` 1) still fails closed |
| altars (raw `0x10`, from floor 4) | **ported** | `handan` case `0x10` → Yes/No → `Button2Click` mode 1 menu → `Button39..42Click`; price `10*(n²-n+2)`; parity on 8 altar witnesses.  The remake used to walk over altars |
| walkable tiles | **corrected** | only `0x06` walks (`handan`); `0x04`/`0x05` (door frames, e.g. floor 10's closed doors), `0x0f`, `0x11` block; `0x0e` stays walkable until the Snow Crystal is ported (known divergence) |
| `moncheck` after battles (floors 2, 8, 10) | **ported** | runs after every C battle win; floors 2/8 open doors, floor 10 opens the gate and places the rewards and the fairy; parity on `moncheck_*` |
| floor 10 (boss room) and the fairy | **ported** | trigger tile (5,4) raw `0x07` → `mevent` → `ichicheck` floor 10 (462-command timeline, or the 32-command thief walk once `0x6a` is set); raw `0x17`/`0x18` → `yousei` (score, rank on the temporary ranking, area, `save0.dat` checkpoint, sub-event 26 → `0x6a`; difficulty 0 shows the ranking window and leaves Timer2 off).  Parity on `floor10*`, `handan_floor10*`, `yousei_*`, `button1click_1a*`.  The floor-entry placeholder is gone.  Ranking files (`optionN.dat`) are not kept yet |

Placeholders that write state indices the original does not use are a
save-compatibility defect; the last one (`0x9b`) was removed with the
floor-3 port.

### Phase 3.5 — Opening escape event (analysis and implementation)

The opening sequence shown by the original game is not a wall interaction.
It is a story/cutscene event with spawned actors, dialogue, a title/cutscene
transition, and a later floor/position transition.  It must be analysed before
it is implemented again.

#### 3.5A — Original-game analysis (oracle-driven; no gameplay trace required)

The source of truth for this phase is the native oracle described in
`11-event-analysis-plan.md` and `14-event-oracle-contract.md`: the original
handlers and `stackwork` executed on synthetic snapshots.  The live-trace log
further below is kept as corroboration, not as a gate.  Remaining analysis
items for the opening path, in order:

1. ~~end-to-end execution of the floor-3 trigger~~ — **done** (P3b,
   `16-event-pilot.md` §8): Zeno dialogue (sub-event 20) → `Button1Click`
   case 20 → `openning2` (651 commands) → sub-event 21 → `openning31`
   capture → sub-events 11/11/12/13; the prisoner marker is already in the
   initial map;
2. ~~realistic trigger case~~ — **done**: the player steps onto `(4,8)`
   (`handan_floor3_adjacent`);
3. ~~`timer2` interval~~ — **done** (`12-event-vm.md` §10);
4. ~~the one-time `open0.bmp` launch boundary (`timer4ontimer`)~~ — **done**
   (`src/intro.c`, `16-event-pilot.md` §10); `openning31` now runs in the
   (tainted) drain;
5. ~~the message-ID mapping~~ — **done** (`18-messages.md`).

* **Floor-3 trigger resolved statically.**  `floor==3 + raw 0x07` enters
  `handan` `0x4447d2` → `mevent` case `0x463a2c`.  The case clears the tile
  `0x07 → 0x06` (one-shot), appends `0x0a`/`0x0a` (selector `0x14`), and calls
  `zenolivework`.  The player remains in place during the trigger; the later
  sub-event presentation still owns any teleport.  There is no separate guard
  flag for the map trigger.  `openning2` is the new-game `Button1Click` path,
  not the floor-3 trigger.  See `16-event-pilot.md`.
* Trace the remaining presentation details from the resolved path; the
  separate new-game `open0.bmp` launch is already recovered (document 16 §10):
  * exact floor/index/raw trigger tile — resolved: floor 3, `(4,8)`, raw `0x07`;
  * the guard flag/index that makes the event one-shot — resolved: tile
    mutation `0x07 → 0x06`, no separate flag;
  * whether any prerequisite state (new game, floor, equipment) is checked —
    floor and raw tile only;
  * actor/message/timing data and the separate `open0` launch boundary.
* Dump the event timeline from the resolved path (`mevent` case `0x463a2c` →
  `zenolivework`) and the separate new-game `openning2`/`openning31` path:
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
  is used for the Zeno cutscene.  Actor positions/timing are approximate; the
  one-time launch `open0.bmp` sequence is ported (`src/intro.c`).

#### 3.5B — Engine support (revised: command VM) — done

Items 1–3 and 5 are implemented (`src/event_vm.c`, `src/event_builders.c`,
`src/event_engine.c`, tests `event_vm`, `event_builders`, `event_engine`);
item 4 has the `0x09` actor and logged stubs for the rest.  Op `0x0a`
pauses the tick until its sub-event returns, matching the original Timer2
suspension (`12-event-vm.md` §11).

1. `src/event_vm.c/.h`: C port of `docs/re/tools/event_vm.py` — command
   buffer, count/cursor, tail/LIFO consumption, opcode table, `0x0b`
   sub-dispatch and cursor splices — with unit tests ported from
   `test_event_vm.py`.
2. A `timer2`-equivalent event tick (up to two commands per tick) driven by a
   render-loop accumulator.  The period is one named constant, provisional
   until 3.5A item 3 is done.  This replaces the planned fixed 60 Hz
   `wait(ticks)` contract.
3. Lua builder API: `emit`, state/tile accessors, the auxiliary globals
   (`message_selector`, `event_selector`, `target_index`, `direction`, …) and
   `call(builder)`.  Builders never yield.
4. Presentation primitives needed by the opening path, named by opcode rather
   than invented per event: `0x01` sound (logged stub until audio exists),
   `0x02` sprite, `0x09` three-sprite animation, `0x03`/`0x0a` the `zyouout`
   window, `0x15` draw actor, and `0x0b` subcommands `7` (title), `8`/`9`
   (player draw/animation), `0xb` (floor change), `0xc` (`openning9`),
   `0xd`/`0xe`/`0xf` (hide UI, hide images, fade).  Actor storage, sprite/mask
   loading and draw ordering belong here.  `spawn_actor`/`move_actor`/
   `cutscene_image` were transitional and are gone with the coroutine runtime.
5. Input is locked while the command count is non-zero; save is rejected for
   the whole event; load/new game clears the buffer, the auxiliaries and all
   presentation state (actors, title/fade, pending animation).

Do not add these as ad-hoc game-state hacks; they belong in the event runtime
and must be covered by tests.

#### 3.5C — Implementation

* ~~Port the `mevent` floor-3 case and `zenolivework`~~ — done; the
  `3:92:7` handler and flag `0x9b` are removed.
* ~~Port `roujin` as a builder~~ — done (parity); route it once its
  dialogues have message IDs.
* ~~Port `Button1Click` case 20 (`openning2`, `openning31`, `openning9`)~~
  — done, with drain parity; `subevent:20` is retired.  (`openning2` is the
  continuation of the Zeno dialogue, not a new-game path.)
* ~~Port the prisoner (`touzoku`, `0x465900`)~~ — done, with drain parity;
  the placeholders are removed.
* ~~Remove the replay shortcut~~ — done: replay rejects the opening event.
* ~~Update the input regression~~ — done.
* Keep the ordinary floor-2 old man separate from the event-spawned prisoner.

#### 3.5D — Verification

* Golden-vector parity (`ctest`, skipped when the locally generated vectors are
  absent): for each ported builder, command words, state/map deltas and
  auxiliaries equal the native oracle on the pilot witnesses listed in
  `16-event-pilot.md`.
* End-to-end vector, once the execution oracle exists: after the buffer
  drains, floor, x/y, stats and map equal the oracle's.
* Regression: the event runs exactly once — the cleared tile `0x07 → 0x06`
  survives save/load and revisiting it does not retrigger — and no state index
  outside the original's writes changes.
* Regression: the command sequence is identical under different render/update
  cadences.
* Regression: loading/cancelling/new-game at any point of the timeline cleans
  up the buffer, actors, title/fade state and input locks.
* Regression: replay rejects the opening event explicitly until event-aware
  replay is implemented.
* Corroboration: the post-event state matches the user-provided post-Orb save
  where the two overlap.

### Phase 4 — Remaining game (post-slice)

* Floors 11–50, remaining NPCs/bosses/secret events, ported handler by
  handler as Lua builders in the order the analysis exports golden vectors
  (`11-event-analysis-plan.md`, P4).  A floor is done when its builders pass
  vector parity; a placeholder script does not count.
* Port the remaining presentation primitives as the handlers need them
  (`monidou`, `dooropen`, battle-hit animation, `banpaiawork`, `taisen2`).
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

* **Event golden vectors**: for each ported builder, compare command words
  and state/map/aux effects with the native oracle on identical synthetic
  snapshots.  Vectors are exported locally by the analysis tools and are not
  committed; the test is skipped when they are absent.
* **State-by-state differential tests** for non-event actions: load each
  `saveN.dat`, perform a scripted action, compare the 684-byte state block and
  6273-byte map block with the original (native oracle where an entry contract
  exists, Wine otherwise).  **In place** for the ordinary map actions:
  `tests/test_game_parity.c` runs `game_move` on the `action_*` witnesses
  (doors with/without keys, keys, every item raw 0x20..0x3c incl. area 2,
  option A and already-owned equipment, stairs incl. the floor limit and
  floor 0, 12 monster fights incl. a hopeless one) and compares the state
  and map with the original's drained `handan`: 70 of 70 match.  It found
  three bugs, now fixed: the player stepped onto an opened door, item 0x24
  was not taken (the original takes it and grants nothing), and floor 1's
  down stairs refused to reach floor 0.  The save checksums are excluded
  (the original only computes them when saving).
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
| Timeline control flow | port handlers as builders and accept them only on golden-vector parity with the native oracle |
| Placeholder events mistaken for specification | keep the provenance table in Phase 3 current; placeholders never count towards floor completion |
| Opening cutscene/actor data | do not implement from screenshots alone; complete the Phase 3.5A trace and add the actor/cutscene primitives before content work |
| Wrong wall interpretation | the floor-3 `3:92:7` handler is provisional; replace it with the analysed opening event in Phase 3.5C |
| Event timing determinism | step the command VM from a `timer2`-equivalent tick with a provisional, named period; replace it with the recovered interval; test cadence independence |
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
13. **Events (revised):** original command VM in C + Lua 5.4 builder scripts
    + TSV static data; Lua vendored under `third_party/lua`; scripts in
    `src/events/`.  Supersedes the coroutine-only decision; see
    `docs/design/01-event-system.md` §7.
14. **Next step:** Phase 4.  The `open0.bmp` launch intro, `taisen2` event
    battles and the coverage checker for the ported handlers
    (`11-event-analysis-plan.md`, P6) are done.  Done additionally: choice
    suspension, merchants, altars, floor 10 and the fairy.  The taisen2
    winning taisen2 battles drain through the ported post-battle `moncheck`;
    losing battles await the death-dialogue suspension.

## Remaining design questions

* When to expose the `*R.bmp` hard-mode/second-run art.
