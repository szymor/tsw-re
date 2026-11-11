# Open questions and next analysis steps

These are the remaining unknowns.  None of them block the core PoC; they can
be resolved incrementally.

## Map

1. **Two-byte gaps between maps — resolved.**  They are stair arrival
   positions: byte 0 is the entry tile when going up to the floor, byte 1 is
   the entry tile when going down to the floor.  `handan` reads them at
   `0x4b8934 + floor*0x7b` and `0x4b8935 + floor*0x7b`.  See
   `02-map-format.md`.
2. **Exact `0x61..0x9d` tile semantics.**  These values are handled in
   `handan`'s `else` branch and by `ichicheck`.  They appear to be NPCs,
   boss markers, and scripted events.  Decode the `ichicheck` jump table at
   `0x46cb8b` and list every floor/position handler.
2b. **`handan` switch cases 7/9/10.**  The switch is on the raw tile value,
   but raw 7/9/10 are wall/floor sprites in the map table.  Determine whether
   those cases are reached through an alternate map layer, a scripted pickup,
   or are effectively dead for normal play.
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
6. **`0x4b87e8`.**  Set to 4 by the initializer and to 1..4 by `formkeydown`;
   it appears to be the facing/step animation frame.  Confirm its role in
   rendering and sound.
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

10. **Full special-item slot → name mapping.**  Slots 0..14 are known
    structurally; the pickup messages and icon indices are known.  Extract
    the message list and align each slot with its name/effect.
11. **Gold multiplier in `taisen`.**  The code multiplies the reward by
    `(0x4b8704 + 1)`.  Is slot 14 a gold item, or is this a bug/another
    variable?  Test by picking up slot 14 and comparing rewards.
12. **Cross / holy equipment effects.**  The manual says the cross halves
    zombie/vampire strength and holy gear is needed for the dragon.  Find the
    checks in `taisen`/`monshyouji` (likely equipment flags or special slots).
13. **Magic attacks and boss special attacks.**  The manual describes
    magicians attacking from adjacent tiles and Zeno's "divide life by two".
    These are in the boss/event scripts, not the basic battle loop.
14. **Rounding.**  Confirm integer division/truncation for all damage and
    reward formulas, especially with the difficulty multiplier.

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

18. **`.oz` decompression.**  `Chtable.oz`, `Chtableb.oz`, `Key.oz` are
    compressed atlases.  Either implement the decoder or require the user's
    extracted PNG/BMPs.
19. **MIDI playback.**  The game uses MCI/media player for MIDI.  Decide
    whether the PoC uses SDL_mixer + timidity/fluidsynth or skips audio.
20. **Ending images.**  `Data2/End05..End26.bmp` are ending frames.  Map the
    ending sequence and triggers.

## Next analysis steps (if requested)

1. Decompile the `handan` `else` branch and `ichicheck` jump table; produce
   an event table keyed by `(floor, mapIndex, tileValue)`.
2. Extract the DFM message list and align message IDs with the item/NPC code.
3. Trace the boss scripts (`yousei`, `mevent`, `madoushi*`, `banpaiawork`)
   and the five area-boss sequences.
4. Diff all eight saves against the initial state to map the event-flag
   region (`0x4b8730..0x4b88fb`).
5. Validate the save checksum and magic with a script that reads/writes all
   eight saves.
