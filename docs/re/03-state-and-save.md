# State block, save, load, options and rankings

## State block

The state block is 684 bytes (`0x2ac`) and starts at runtime `0x4b8688`.
It is also the second block of every save file.  Offsets below are relative
to `0x4b8688`.

| Offset | Absolute | Type | Meaning |
|---:|---:|---|---|
| `0x000` | `0x4b8688` | int32 | life |
| `0x004` | `0x4b868c` | int32 | attack |
| `0x008` | `0x4b8690` | int32 | defense |
| `0x00c` | `0x4b8694` | int32 | gold |
| `0x010` | `0x4b8698` | int32 | current floor (1..50) |
| `0x014` | `0x4b869c` | int32 | highest floor ever reached (orb-of-flying bound) |
| `0x018` | `0x4b86a0` | int32 | player x (0..10) |
| `0x01c` | `0x4b86a4` | int32 | player y (0..10) |
| `0x020` | `0x4b86a8` | int32 | yellow keys |
| `0x024` | `0x4b86ac` | int32 | red keys |
| `0x028` | `0x4b86b0` | int32 | blue keys |
| `0x02c` | `0x4b86b4` | int32 | unknown (usually 0) |
| `0x030` | `0x4b86b8` | int32 | battle/render flag |
| `0x034` | `0x4b86bc` | int32 | unknown (0) |
| `0x038` | `0x4b86c0` | int32 | unknown (0) |
| `0x03c` | `0x4b86c4` | int32 | displayed weapon level (0..5) |
| `0x040` | `0x4b86c8` | int32 | displayed shield level (0..5) |
| `0x044` | `0x4b86cc` | int32 | special item slot 0 (orb of the hero) |
| `0x048` | `0x4b86d0` | int32 | special item slot 1 |
| `0x04c` | `0x4b86d4` | int32 | special item slot 2 |
| … | … | int32 | slots 3..13 |
| `0x07c` | `0x4b8704` | int32 | special item slot 14 |
| `0x080` | `0x4b8708` | int32 | sword 1 picked (Iron) |
| `0x084` | `0x4b870c` | int32 | sword 2 picked (Silver) |
| `0x088` | `0x4b8710` | int32 | sword 3 picked (Knight) |
| `0x08c` | `0x4b8714` | int32 | sword 4 picked (Holy) |
| `0x090` | `0x4b8718` | int32 | sword 5 picked (Sacred) |
| `0x094` | `0x4b871c` | int32 | shield 1 picked (Iron) |
| `0x098` | `0x4b8720` | int32 | shield 2 picked (Silver) |
| `0x09c` | `0x4b8724` | int32 | shield 3 picked (Knight) |
| `0x0a0` | `0x4b8728` | int32 | shield 4 picked (Holy) |
| `0x0a4` | `0x4b872c` | int32 | shield 5 picked (Sacred) |
| `0x0a8`+ | `0x4b8730`+ | int32[] | event / treasure / monster flags |
| `0x274` | `0x4b88fc` | int32 | save-format magic, must be `-4` |
| `0x278` | `0x4b8900` | int32 | last vertical move direction: 1 up, 2 down (Orb of Flying placement; `17-static-findings.md` §7) |
| `0x27c` | `0x4b8904` | int32 | option flag A (difficulty/stat carry) |
| `0x280` | `0x4b8908` | int32 | option flag B (item carry) |
| `0x290` | `0x4b8918` | int32 | checksum: sum of ints 0..163 |
| `0x294` | `0x4b891c` | int32 | checksum: alternating sum |
| `0x298` | `0x4b8920` | int32 | unknown metadata |
| `0x29c` | `0x4b8924` | int32 | unknown metadata; set to 1 by `Button11Click`…`Button35Click` before `zyouout` |
| `0x2a0` | `0x4b8928` | int32 | unknown metadata |
| `0x2a4` | `0x4b892c` | int32 | unknown metadata |
| `0x2a8` | `0x4b8930` | int32 | save format version = 12 |

The checksum covers exactly the first 164 int32s (`0x000`..`0x28f`).  The
alternating checksum is `sum((-1)^i * value[i])`, implemented with the usual
Delphi `and $80000001` sign trick.

The runtime new game has the magic `-4` and version `12` already set.  They
are written by `formactivate` (`0x44280c`) at startup, not by `syokidata`:
it stores `-4` to `0x4b88fc`, clears both option flags `0x4b8904` /
`0x4b8908`, stores `12` to `0x4b8930`, reads `option.dat` (see below), and
then calls `syokidata` and `syokidata2`.

## New-game values (`syokidata`, `0x455cb8`)

```
life = 1000, attack = 100, defense = 100, gold = 0
floor = 1, floor limit = 1, x = 5, y = 10
keys = 0
all 15 special item slots = 0
weapon display level = 5, shield display level = 5
sword 5 picked = 1, shield 5 picked = 1
all other equipment flags = 0
0x4b87e8 = 4
```

The initializer resets stats/gold/floor-limit/items only when the option
flags `0x4b8904` / `0x4b8908` are zero.  `GameStart1Click` clears both
flags before calling `gameover`, so a normal new game performs the reset.

> Not a quirk (corrected): the display level 5 and the “picked” flags for the
> Sacred sword and shield belong to the **opening segment** before Zeno.  The
> attack/defense of 100/100 are the opening values; the pickup routine adds
> bonuses only for items picked up in play.  `openning31` (`0x4653b8`,
> timeline sub-event 1) later moves the player to floor 2 at (2, 7), sets
> 400/10/10 (when option A is 0), clears both display levels and the Sacred
> sword/shield flags.  A re-implementation should keep the opening state and
> apply that demotion in the Zeno sequence.  See `17-static-findings.md` §1.

## Save file writing (`savework`, `0x47eadc`)

1. Compute the two checksums over the 164 state ints and store them at
   `0x4b8918` / `0x4b891c`.
2. Open the path in `DAT_0048c5d4`.
3. Ask “Overwrite. Is it true?”.
4. Write `0x1881` bytes from `0x4b8934` (the map block).
5. Write `0x2ac` bytes from `0x4b8688` (the state block).
6. Set `_DAT_0048c58c = 0x86` and refresh the UI.

The slot handlers (`Save11Click` … `Save81Click`, `0x47ecc4` …) set
`DAT_0048c5d4` to `\save1.dat` … `\save8.dat` and call `savework`.
`OptionSave1Click` (`0x47d324`) is **not** a save-slot handler: it writes
the separate options file `\option.dat`.

## Save file loading (`loadwork`, `0x47e7d4`)

1. Open `DAT_0048c5d4`.
2. Read `0x1881` bytes into `0x4b8934` (map).
3. Read `0x2ac` bytes into `0x4b8688` (state).
4. If `state[0x274] == -4`, recompute the two checksums and compare them with
   `state[0x290]` / `state[0x294]`.
5. On success, call `syokidata2` and `disp` and continue.
6. On failure, overwrite the path with `\save0.dat` and show
   `"Donot use this save data."`.

`save0.dat` is the **checkpoint/autosave** written by the fairy routine
`yousei` at area bosses.  Its stored checksums are zero, so it is not a
normal validated slot.

## Options file (`option.dat`)

`OptionSave1Click` writes 10 bytes starting at `DAT_00489b9c`:

```
byte 0 : value read from the component at form +0x3c8
byte 1 : value read from the component at form +0x1c8
byte 2 : 0
byte 3 : 2 by default; 1 if form +0x3a8 checked; 3 if form +0x3b0 checked
bytes 4..9 : current contents of the buffer (see the writers below)
```

The buffer is a live settings block, not only a save format.
`formactivate` reads the 10 bytes of `option.dat` into `0x489b9c` at
startup, and several menu handlers write individual bytes directly:

| Byte | Address | Writers seen in the disassembly |
|---:|---:|---|
| 0, 1, 2 | `0x489b9c..9e` | size menu: `tiny1Click` 08/0a/00, `Small1Click` 09/0b/00, `Normal1Click` 0a/0c/00, `Large1Click` 0c/0e/00.  Byte 1 is later passed to the message panel's font (`Button36Click`). |
| 4 | `0x489ba0` | `KeyboardControl1Click` = 1, `MouseControl1Click` = 0 |
| 5 | `0x489ba1` | `K5Click` toggles 0/1 together with the check item at form `+0x420` |
| 7 | `0x489ba3` | `wavon1Click` toggles 0/1 together with `0x4b87ec` (WAV on/off) |
| 8 | `0x489ba4` | 1 = 800×500 layout; `N640x4001Click` and `NormalOption1Click` reset it to 0 and re-lay out the form |
| 6, 9 | `0x489ba2`, `0x489ba5` | only reset to 0 by `NormalOption1Click` |

`NormalOption1Click` (`0x47f5e8`) restores the defaults
`0a 0c 00 02 00 01 00 00 (ba4) 00`.  The size menu items are the check
items at form `+0x3bc` (Large), `+0x3c0` (Small), `+0x3c4` (Normal) and
`+0x3f4` (tiny).

The exact UI type of the two components is unresolved: the same form offsets
are used by `disp` for the life and weapon-name images.  They may belong to a
different form or the option-control mapping may be more indirect than it
looks.  The byte-3 flags correspond to the mouse/keyboard/display option
group.  The option flags `0x4b8904` / `0x4b8908` used by `syokidata` are
separate globals set by the options menu, not by this file.

## Ranking files

`option1.dat` … `option5.dat` are the per-area high-score tables.  Each file
is 288 bytes:

```
10 rows × 6 int32 = 240 bytes
  int32 score
  int32 life
  int32 attack
  int32 defense
  int32 sum          (= score + life + attack + defense)
  int32 unknown      (secondary score/checksum; see below)

6 int32 column sums = 24 bytes
  sum(score), sum(life), sum(attack), sum(defense), sum(sum), sum(unknown)

6 int32 footer = 24 bytes
  five unresolved values + int32 area magic = -(400 + area)
```

Total: 240 + 24 + 24 = 288 bytes.

The row format was confirmed against the save files: e.g. `option3.dat`
row 0 is `[214, 2595, 387, 465, 3661, 1202]`, and `save4.dat` has
life/attack/defense `2595/387/465`.  The first field is the **fairy score**;
the fifth is the sum of the first four (verified for all rows).

The sixth field is **not** the alternating checksum of the first four:
for `[214, 2595, 387, 465]` the alternating sum is `-2459`, while the file
stores `1202`.  It may be a secondary score or a different checksum; mark it
unresolved until `yousei` is fully decoded.

The footer's last int is a clear area magic: `-401` … `-405` for
`option1.dat` … `option5.dat`.  The other five footer ints are unresolved
(possibly a previous-table backup or aggregate).

The area files are:

| File | Area boss floor |
|---|---:|
| `option1.dat` | 10 |
| `option2.dat` | 20 |
| `option3.dat` | 25 |
| `option4.dat` | 40 |
| `option5.dat` | 49 |

`yousei` (`0x478f0c`) computes the score, inserts it into the table, writes
the table, and writes `save0.dat` as a checkpoint.

### Fairy score (observed shape)

At each boss floor the score is a base 60 plus weighted life/attack/defense
and keys, minus a difficulty/area baseline:

```
score = 60
      + f(life, difficulty, area)
      + a(attack, difficulty, area)
      + d(defense, difficulty, area)
      + yellowKeys*1 + blueKeys*2 + redKeys*3
```

The exact per-floor constants are in `yousei`:

* floor 10: `(life + (diff+1)*-400)/50 + attack*3 + defense*2 + ...`
* floor 20: `(life + (diff+1)*-800)/100 + attack*2 + defense + ...`
* floor 25: `(life + ...)/400 + attack + defense/3 + ...`
* floor 40: `(life + ...)/200 + attack + defense + ...`
* floor 49: `(life + (diff+1)*-3000)/400 + attack + defense/3 + ...`

See `04-game-logic.md` for the key-color mapping.
