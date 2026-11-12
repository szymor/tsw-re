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
   input/NPC call graph in `13-event-triggers.md`.  The remaining work is each
   handler’s command timeline plus the per-case `mevent` guards and the
   NPC/event branches in `handan`’s `else` path.
2b. **`handan` switch cases 7/9/10 — partly resolved.**  They are scripted
   action classes invoked by event code with a synthetic target tile, not
   normal walk-on tiles.  Case 9 is the pressable raw-`0x09` secret wall
   (runtime message `0x89` maps to “The wall collapsed!” via the observed
   `-0x0b` offset), not the orb-of-flying pickup.  List the remaining
   scripted call sites.
2c. **Door entry timing — open.**  Saved maps show opened doors end as raw
    `0x06`, but do not show whether the original enters the door cell on the
    same arrow press.  Phase 1 consumes the key and moves in one step; confirm
    with a differential action trace.
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
8. **Options bytes 4..9.**  `option.dat` is 10 bytes; only bytes 0..3 are set
   by `OptionSave1Click`.  Check a real options file after changing all menu
   options.
9. **Option flags `0x4b8904` / `0x4b8908`.**  They act as difficulty/stat and
   item carry-over multipliers.  Map them to the exact menu items (Normal/
   High/Middle/Low, etc.).
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
    These are in the boss/event scripts, not the basic battle loop.
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
    floor-travel flow remain Phase 3 polish.
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
16. **Message list indexing.**  The game uses numeric message IDs; extract
    the DFM string list and build an `id → text` table locally.
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

20b. **Event-flag indices — partially confirmed.**  P2.5 confirmed the floor-2
    old-man flag at state index `0x62` (`0x4b8810`) and the floor-10 area-1
    completion flag at `0x6a` (`0x4b8830`).  `0x43..0x45` are also
    `DAT_0048c55c` selector values in `roujin`, so the floor-3/4/6 state
    flags are **not** confirmed yet; see `15-event-flags.md`.
20c. **Area-1 ranking write — open.**  The fairy computes a provisional score
    and writes a zero-checksum `save0.dat` checkpoint, but ranking writes stay
    disabled until the sixth field and footer are decoded.
20c-2. **Opening escape event — trigger resolved.**  The floor-3 `(4,8)`
    raw-`0x07` action enters `handan` `0x4447d2` → `mevent` case `0x463a2c`,
    clears the tile to `0x06` (one-shot), appends `0x0a`/`0x0a` (selector
    `0x14`), and calls `zenolivework`.  The map action itself keeps the player
    in place; the later teleport to the floor-2 prison and the exact
    actor/timeline data remain tied to the sub-event presentation.  The
    post-Zeno prisoner event is floor-2 raw `0x15` at `(2,6)`; the marker moves
    to `(0,7)` for the second dialogue before disappearing.  The one-time
    `open0.bmp` launch remains separate.  See Phase 3.5A and
    `16-event-pilot.md`.

20d. **Initial event scripts — pilot resolved, bulk pending.**  Old-man
    first/repeat dialogues for floors 2/3/4/6 are taken from the original game;
    the floor-2 flag is `0x62`, and the floor-3/4/6 selectors are recovered as
    `0x1a/0x43`, `0/0x44`, and `1/0x45` (message/event selector).  The
    floor-16 old-man gate is `0x6d`.  `0x13` is the merchant and `0x19` the old
    man; `0x15` is **not** an altar (its handler `FUN_00465900` is a map
    event).  Pressable `0x09` secret walls are implemented for floor 5 `(8,10)`
    and floor 9 `(9,4)`.  The pilot floor-10 timelines are differentially
    validated for the armed/guard-disabled/nonzero-sound witnesses; merchant/
    altar scripts and bulk handler coverage remain.

21. **Static inventory vs control flow — pilot composer present.**  The
    original `extract_timelines.py` is still an address-window inventory.  P0
    added `scan_event_buffers.py`, which reports 2,656 append sites and 0
    unresolved candidates on the pinned binary.  P3 added the trace-free
    `compose_event_timeline.py` pilot composer, which reproduces the floor 10,
    `roujin`, `openning2`, `zenolivework`, and floor-3 trigger streams exactly.
    Bulk builder/CFG lifting remains P4.
22. **Runtime message IDs vs DFM ordinals.**  Build a verified mapping table.
    The equipment pickup list uses runtime IDs such as `0x9d` for the text
    that appears at DFM ordinal `0x92`; the special-item use list is related
    but not identical.
23. **Opcode `0x14` state indices — resolved.**  The opcode writes
    `state[arg1] = arg2` directly at base `0x4b8688`, so the indices are the
    `GameState` indices documented in `03-state-and-save.md`; see
    [`12-event-vm.md`](12-event-vm.md).

P1 also resolved opcode `0`/`0x12` (both no-ops; `0` is the one-invocation
frame step), the `0xb` sub-event mechanism (cursor splice, not a stack), and
the `timer2timer` two-command ceiling.  See [`12-event-vm.md`](12-event-vm.md).

## Next analysis steps (if requested)

The consolidated trace-free plan for the event system is in
[`11-event-analysis-plan.md`](11-event-analysis-plan.md).  The short list below
remains the high-level order.

1. Timeline extraction is implemented (`tools/extract_timelines.py`) and
   summarized in `10-events-and-items.md`.  Next: annotate each command with
   message/sprite IDs and produce the final data-driven event table keyed by
   `(floor, mapIndex, tileValue)`.
2. Extract the DFM message list and align message IDs with the item/NPC code.
3. Trace the boss scripts (`yousei`, `mevent`, `madoushi*`, `banpaiawork`)
   and the five area-boss sequences.
4. Diff all eight saves against the initial state to map the event-flag
   region (`0x4b8730..0x4b88fb`).
5. Validate the save checksum and magic with a script that reads/writes all
   eight saves.
