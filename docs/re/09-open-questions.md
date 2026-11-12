# Open questions and next analysis steps

These are the remaining unknowns.  None of them block the core PoC; they can
be resolved incrementally.

## Map

1. **Two-byte gaps between maps — resolved.**  They are stair arrival
   positions: byte 0 is the entry tile when going up to the floor, byte 1 is
   the entry tile when going down to the floor.  `handan` reads them at
   `0x4b8934 + floor*0x7b` and `0x4b8935 + floor*0x7b`.  See
   `02-map-format.md`.
2. **Exact `0x61..0x9d` tile semantics — partly resolved.**  The per-floor
   `ichicheck` dispatch table is decoded in `10-events-and-items.md` (floors
   10, 15, 20, 24, 25, 30, 32, 33, 38, 40, 42, 43, 48, 49).  P2 also decoded
   the `handan`, `mevent`, `roujin` and `moncheck` switch tables and the
   input/NPC call graph in `13-event-triggers.md`.  What is left is tracked
   by the coverage matrix (`16-event-pilot.md` §9): the per-case `mevent`
   guards and the `handan` raw families without witnesses.
2b. **`handan` raw `0x07`/`0x09`/`0x0a` — mostly resolved.**  They are
   bump triggers, not walls: `0x07` → `mevent` (floor-3 Zeno trigger and the
   `ichicheck` floors), `0x09` → the secret walls (`ListBox2[0x89]`, “The
   wall collapsed!”), both ported.  `0x0a` (`mevent` case 10) has no witness
   yet.  See `10-events-and-items.md`.
2c. **Door entry timing — resolved.**  The original opens the door and
    leaves the player in place; the `action_*` parity witnesses caught the
    remake stepping onto the opened door, now fixed (`test_game_parity`).
3. **Floor 50 / ending.**  Floor 50 is normally unreachable; the text says
   the tower is 50 stories but you cannot reach 50.  Verify how the ending
   is triggered and whether floor 50 exists as a playable map.

## State / save

4. **Last five ints of the state block** (`0x4b8920..0x4b8930`).  This range
   contains four unknown metadata ints (`0x4b8920..0x4b892c`) followed by the
   known version `12` at `0x4b8930`.  The save magic `-4` is at `0x4b88fc`,
   outside this range.  The four unknown ints may be timestamps, play time,
   or slot metadata; compare saves made at different times.
5. **`0x4b869c` (floor limit) — mostly resolved.**  It is the highest floor
   ever reached.  The stairs transition updates it with
   `floor_limit = max(floor_limit, floor)`, and the orb-of-flying travel in
   `timer3ontimer` uses it as the upper bound.  Still confirm whether the
   five area-boss events also adjust it.
6. **`0x4b87e8` (facing) — resolved.**  Values are 1=down, 2=left, 3=right,
   4=up.  The player rendering routine uses the adjacent Chtable color/mask
   pairs 184/185, 192/193, 200/201 and 208/209; see `04-game-logic.md`.
   Exact original step-animation timing and sound remain optional polish.
7. **`0x4b88fc` magic and `save0.dat`.**  `save0.dat` has zero checksums and
   is loaded through a special path.  Determine whether the game validates it
   at startup or only via the fairy checkpoint.
8. **Options bytes 4..9.**  `option.dat` is 10 bytes.  Bytes 0..2, 4, 5, 7
   and 8 are now mapped to menu handlers (`03-state-and-save.md`); bytes 6
   and 9 have no known writer except the reset to 0.  Still check a real
   options file after changing all menu options.
9. **Option flags `0x4b8904` / `0x4b8908`.**  They act as difficulty/stat and
   item carry-over multipliers.  Map them to the exact menu items.  Note that
   High/Middle/Low are **not** these flags: `High1Click`/`Middle1Click`/
   `Low1Click` only call `speedhigh`/`speedmiddle`/`speedlow` (game speed).
   `formactivate`, `GameStart1Click` and timeline op `0x16` clear both flags.
9b. **Ranking file sixth field and footer.**  Each row's fifth int is the
    sum of the first four, but the sixth is not the alternating checksum
    (`[214,2595,387,465]` would give `-2459`, not `1202`).  The 288-byte file
    has 10 rows + 6 column sums + 6 footer ints; the footer's last int is
    `-(400+area)`, but the other five are unknown.  Decode `yousei`'s table
    update code.
9c. **Option controls at `+0x3c8` / `+0x1c8`.**  `OptionSave1Click` reads
    those form offsets, but `disp` uses the same offsets for the life and
    weapon-name images.  Resolve whether the options handler belongs to a
    different form or the mapping is indirect.

## Items / battle

10. **Special-item slot → name mapping — resolved.**  The full 15-slot table
    is in `10-events-and-items.md`.  Slot 6 is the Bomb and slot 7 is the
    Warp Staff; the use-message order and handler order agree.  The value
    `3` in slot 7 is most likely warp-staff charges.  `itemdisp` confirms the
    inventory icons use Chtable sprite `0x23 + slot`; the implementation uses
    that mapping and treats slot 7 as three charges.
11. **Gold multiplier in `taisen` — resolved.**  Slot 14 (`0x4b8704`) is the
    Lucky Gold item; the battle reward is multiplied by `(slot14 + 1)` and
    the implementation follows that rule.
12. **Cross / holy equipment effects — resolved.**  The cross is slot 3
    (`0x4b86d8`) and the Dragon Slayer is slot 10 (`0x4b86f4`).  Both make
    the player's attack multiplier `2` against their target classes:
    cross vs Zombie (12), Zombie Knight (13), Vampire (17); Dragon Slayer vs
    Dragon (19).  See `06-data-tables.md`.
13. **Magic attacks and boss special attacks.**  The manual describes
    magicians attacking from adjacent tiles and Zeno's "divide life by two".
    The adjacency attacks are `idou`'s `mazyutu1` (raw `0x97..0x9a`) and
    `madoushi1` (Magic Sergeant pincer) checks; recovered but not ported
    (`08-implementation-plan.md`, Phase 4).  Opcode `0x13` halves life.
14. **Rounding — resolved for combat.**  The battle simulation is pure
    integer add/subtract; there is no fractional division.  Item formulas
    such as `((floor+9)/10)*50*(diff+1)` use integer division, which is
    already documented.
14b. **Sacred Sword access route — resolved.**  The item is on floor 13 at
    (5,4), surrounded by **lava** (`0x0d`).  It is not reachable early; the
    **Snow Crystal** (slot 11), obtained late in the game, makes the lava
    passable.  See `10-events-and-items.md`.
14c. **Snow Crystal activation — implemented provisionally.**  The use
    handler is wired to convert every lava tile on the current floor to the
    ice tile `0x0e`.  Confirm whether the original changes the whole floor or
    only selected lava tiles.
14d. **Magic Elixir formula — open.**  The implementation uses
    `life += (attack + defense) * 10`, based on the old man's hint that the
    elixir grants life from offensive and defensive power.  The exact
    original formula still needs a differential test.
14e. **Warp Staff destination — open.**  The implementation warps to the
    floor's up-stairs tile.  The original may use a different destination or
    a random empty tile; verify with a controlled use.
14f. **Orb UI — partly open.**  Orb of the Hero opens a modal forecast view
    listing every distinct normal monster on the floor with effective stats and
    predicted damage/reward.  Orb of Wisdom still logs a placeholder, and Orb
    of Flying's travel UI is not implemented; the original windows and
    floor-travel flow remain Phase 5 polish.
14g. **Other special-item effects — provisional.**  The implementation has
    Bomb kill adjacent monsters (no gold), Magic Mattock clear adjacent walls,
    Super Mattock/Magic Key clear all walls/doors, Snow Crystal freeze all
    lava, and Wings consumed after one trip.  Verify exact scope and
    consumption with the original.
14h. **Extreme stat policy — defined.**  Imported states can contain very
    large int32 values.  Item gains and battle rewards/damage saturate at the
    int32 bounds before state mutation, so they cannot wrap.  This is a
    reimplementation safety rule, not yet compared with the original.

## UI / text

15. **Invisible stat text under Wine.**  Wine's font trace confirms the game
    draws `"100"`/`"1000"` with Verdana, but the pixels stay white.  Find the
    text color/background combination (likely a VCL canvas quirk).  Not
    required for the PoC; render text with SDL_ttf.
16. **Message list indexing — resolved for dialogues.**  See item 22 and
    [`18-messages.md`](18-messages.md).
17. **Menu accelerators under synthetic X11.**  Alt/menu clicks did not work
    in the nested session.  The handlers are known; test on a real desktop
    before assuming a Wine bug.

## Assets

18. **`.oz` atlases (resolved).**  `Chtable.oz`, `Chtableb.oz` and `Key.oz`
    are 8-bit indexed BMP containers with a `.oz` extension.  The conversion
    script decodes them directly; no custom decompressor is needed.
19. **MIDI playback.**  The game uses MCI/media player for MIDI.  Decide
    whether the PoC uses SDL_mixer + timidity/fluidsynth or skips audio.
20. **Ending images.**  `Data2/End05..End26.bmp` are ending frames.  Map the
    ending sequence and triggers.

## Timeline / event open items

20b. **Event-flag indices — pilot confirmed.**  The floor-2 old-man one-shot
    is `0x62` (`0x4b8810`); the old men's talked-to flags are the event
    selectors `0x42..0x45` (item 26); `0x50 + id` is the "dialogue sub-event
    confirmed" flag written by `Button1Click`, which is how the floor-10
    area-1 flag `0x6a` (sub-event 26) is set (item 25).
20c. **Ranking writes — open.**  The fairy (`yousei`, ported) computes the
    score, ranks it on the temporary ranking and writes the zero-checksum
    `save0.dat` checkpoint; ranking files stay unwritten until the sixth field
    and footer are decoded (item 9b).
20c-2. **Opening escape event — resolved and ported.**  The whole chain
    runs on builders with golden-vector and drain parity
    (`16-event-pilot.md` §8): trigger (`mevent` case `0x463a2c`, item 27) →
    Zeno → sub-event 20 → `Button1Click` case 20 → `openning2` → sub-event
    21 → `openning31` (floor 2 at (2, 7), 400/10/10, Sacred gear cleared,
    `17-static-findings.md` §1) → sub-events 11, 11, 12, 13.  The prisoner
    is `touzoku` (raw `0x15`).  The `open0.bmp` launch is `timer4ontimer`
    (`src/intro.c`, `17-static-findings.md` §3).
20d. **Initial event scripts — pilot resolved.**  Old men (`roujin`
    selectors and flags, item 26 and `15-event-flags.md`), merchants, altars,
    the thief, secret walls and floor 10 are ported; see the event table in
    `08-implementation-plan.md` (Phase 3).  `0x15` is **not** an altar (its
    handler `0x465900` is the thief/prisoner map event).

21. **Static inventory vs control flow — pilot composer present.**  The
    original `extract_timelines.py` is still an address-window inventory.
    `scan_event_buffers.py` classifies every buffer writer (item 24).  P3
    added the pilot composer (`compose_event_timeline.py`), now frozen; bulk
    handlers are covered by oracle witnesses and `check_event_coverage.py`
    (`11-event-analysis-plan.md` §0).
22. **Runtime message IDs vs DFM ordinals — resolved for event
    dialogues.**  All event dialogues come from `ListBox2.Items` (form
    `+0x44c`): NPC dialogues at `g_dialog_selector + 0x3e`, opcode-`0x0a`
    sub-events at `arg1 − 1`, with three appending cases
    ([`18-messages.md`](18-messages.md)).  Status-panel texts (equipment
    pickups such as runtime `0x9d` vs DFM ordinal `0x92`, special-item use)
    use other lists and remain to be mapped with their handlers.
23. **Opcode `0x14` state indices — resolved.**  The opcode writes
    `state[arg1] = arg2` directly at base `0x4b8688`, so the indices are the
    `GameState` indices documented in `03-state-and-save.md`; see
    [`12-event-vm.md`](12-event-vm.md).

24. **Append-site count — resolved.**  Re-running
    `scan_event_buffers.py --summary` on the pinned binary reports 2,862
    command-opcode writes (append sites), 0 unresolved candidates; 2,656 was
    the stale first-pass count.  See `12-event-vm.md` §8.
25. **Floor-10 flag setter — resolved (P3b).**  `0x6a` is written at
    `0x44da2a` by `Button1Click`, which marks every confirmed dialogue
    sub-event as `state[0x50 + id] = 1` (for `10 < id < 0x44`).  The fairy
    (`yousei`, `0x478f0c`) appends `0x0a/16` = sub-event 26, and
    `0x50 + 26 = 0x6a`.  The floor-10 builder, its 462-command drain and
    `Button1Click` case 23 do not write it.  See `16-event-pilot.md` §8.
26. **`0x42..0x45` selector vs state index — resolved.**  Both.  `roujin`
    writes `state[event_selector]` at `0x449cc0` (1, or 2 with the Orb of
    Wisdom): floor 2 writes `0x42` *and* its one-shot `0x62`; floors 3/4/6
    use `0x43/0x44/0x45` as their talked-to flags.  The builder port and its
    golden-vector parity encode this (`15-event-flags.md` §5).
27. **Floor-3 trigger movement — resolved.**  The player steps onto `(4,8)`:
    `handan` → `mevent` calls `idou` before clearing the tile.  The
    `handan_floor3_adjacent` vector (entered from `(4,9)` moving up) changes
    `y` 9 → 8 and matches the builder port.
28. **`timer2` interval — resolved (static).**  Form default 300 ms; the speed
    menu sets 150/250/350 ms (default 250); `dooropen` (op `0x0d`) sets
    8/10/12 ms until its last frame calls `timerin` (back to the speed value)
    and `kaidanwork` 2/6/10 ms until the next setter.  See
    `12-event-vm.md` §10.  `dooropen` also writes the door tile, which the
    reference VM now models.
29. **Modal suspension — resolved for dialogues (P3b).**  Opcode `0x0a`
    disables Timer2 (`0x453194`: `TTimer.SetEnabled(form+0x2ec, False)`),
    sets `g_subevent_id` (`0x48c5a4`) to `arg1 + 10` and opens the `zyouout`
    window.  The timeline resumes when the player confirms: `Button1Click`
    (`0x44da10`) dispatches on `g_subevent_id` (27-entry table at
    `0x44da4f`), may append a new timeline, and ends in `timer2on`
    (`0x44bc0c`), which re-enables Timer2.  Choice suspension is resolved
    (merchants/altars).  Event-driven battles (`taisen2`, sub-dispatch
    `0xb/10`) are a self-driving command timeline, ported as
    `builders.taisen2`; winning battles drain through the ported post-battle `moncheck`;
    losing battles await the death-dialogue suspension.
30. **Placeholder events in the engine — resolved.**  None remain: every
    event runs a ported builder, and unported paths fail closed (unported
    splices are counted in `EventEngine.unported`).

P1 also resolved opcode `0`/`0x12` (both no-ops; `0` is the one-invocation
frame step), the `0xb` sub-event mechanism (cursor splice, not a stack), and
the `timer2timer` two-command ceiling.  See [`12-event-vm.md`](12-event-vm.md).

## Next analysis steps

The event-analysis order is in
[`11-event-analysis-plan.md`](11-event-analysis-plan.md) §13 and the engine
porting order in [`08-implementation-plan.md`](08-implementation-plan.md)
(Phase 4).  Non-event items above (options, ranking footer, item effects,
ending) are resolved as their handlers are ported.
