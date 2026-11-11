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
   10, 15, 20, 24, 25, 30, 32, 33, 38, 40, 42, 43, 48, 49).  The remaining
   work is to decode each handler’s command timeline and the NPC/event
   branches in `handan`’s `else` path.
2b. **`handan` switch cases 7/9/10 — resolved enough.**  They are scripted
   action classes invoked by event code with a synthetic target tile, not
   normal walk-on tiles.  Case 9 is the orb-of-flying scripted pickup
   (message `0x89`).  List the remaining scripted call sites.
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

21. **Static inventory vs control flow.**  `extract_timelines.py` inventories
    append sites in address windows; it does not follow branches or shared
    tails.  The final event table needs a control-flow-aware pass or runtime
    traces.
22. **Runtime message IDs vs DFM ordinals.**  Build a verified mapping table.
    The equipment pickup list uses runtime IDs such as `0x9d` for the text
    that appears at DFM ordinal `0x92`; the special-item use list is related
    but not identical.
23. **Opcode `0x14` state indices.**  Map the original state-array indices to
    the logical `GameState` layout before implementing the event VM or
    translating timelines.

## Next analysis steps (if requested)

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
