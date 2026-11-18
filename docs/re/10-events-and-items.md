# Events and special items

The per-floor event dispatcher, the event placements and the 15
special-item slots.  Every handler here is ported and checked against the
original (`16-event-pilot.md`).

## Per-floor event dispatcher (`ichicheck`, `0x46cb1c`)

`ichicheck` runs only for floors 10..49:

```
eax = floor - 10
if eax > 0x27: return
index = byteTable[eax]          # 0x46cb63, 40 bytes
jump  jumpTable[index]          # 0x46cb8b, 15 entries
```

The byte table maps a floor to an event index (0 = no event).  The jump table
is:

| Index | Handler | Floor(s) | Event category |
|---:|---:|---|---|
| 0 | `0x4770a1` | — | return / no event |
| 1 | `0x46cbc7` | 10 | area 1 boss / fairy / ranking |
| 2 | `0x46f82e` | 15 | story/secret |
| 3 | `0x46f8f4` | 20 | area 2 boss / fairy / ranking |
| 4 | `0x4704eb` | 24 | story |
| 5 | `0x4727b2` | 25 | area 3 boss / fairy / ranking |
| 6 | `0x4728ef` | 30 | story |
| 7 | `0x472a2c` | 32 | story |
| 8 | `0x4735a4` | 33 | story |
| 9 | `0x47382c` | 38 | story |
| 10 | `0x473961` | 40 | area 4 boss / fairy / ranking |
| 11 | `0x475fba` | 42 | story/secret |
| 12 | `0x47687e` | 43 | story/secret |
| 13 | `0x476cdd` | 48 | story |
| 14 | `0x476dc0` | 49 | final boss / fairy / ending |

The floors **10, 20, 25, 40 and 49** are also the five `yousei` ranking/
checkpoint floors (`option1..5.dat`), which matches the event categories.

## Event timeline mechanism

Event handlers do not call gameplay functions directly.  They append 6-byte
commands (`opcode, arg1, arg2`, all `u16`) to the array at `DAT_0048c74c`,
with the count at `DAT_0048c5ac`; `stackwork` (`0x4526c0`) consumes them from
the tail.  The opcode table, the `0x0b` sub-dispatch, cursor splices and the
`0x14` state indices are documented in [`12-event-vm.md`](12-event-vm.md).

## `handan` raw `0x07` / `0x09` / `0x0a`

These raw values look like wall/floor sprites but are bump triggers: the
player walks into them and `handan` dispatches before the normal wall block.

* raw `0x07` → `mevent` (`0x4447d2`).  On floor 3 the tile at `(4,8)`
  (index 92) is the Zeno trigger: case `0x463a2c` moves the player onto it
  (`idou`), clears it to `0x06` (the one-shot mechanism; there is no
  separate flag) and runs `zenolivework`; the player stays there for the
  dialogue.  On the `ichicheck` floors `mevent` moves
  the player (`idou`), clears the tile and runs the floor's `ichicheck` case.
* raw `0x09` → case 9 (`0x4447df`/`0x444803`): the secret walls on floor 5
  `(8,10)` (beside the Iron Sword) and floor 9 `(9,4)`.  It shows status
  text `0x89` (DFM ordinal `0x89 - 0x0b = 0x7e`, “The wall collapsed!”) and
  opens the wall with `doorstackin` kind 7; the player does not move.  The
  earlier Orb-of-Flying interpretation compared numbers from different lists
  and was wrong.
* raw `0x0a` → `mevent` case 10 (`0x4448ce`): the hidden passages on
  floors 12/16/19, and on floor 41 a magician at `0x14` once cell 12 is
  floor (`16-event-pilot.md` §12c).

## Opening and prisoner events

* **Prisoner (floor 2, raw `0x15`).**  A moving event actor encoded on the
  map, not a static NPC and not an altar.  The first dialogue runs in the
  cell (sub-event 14; the cell door at index `0x43` opens first) and moves
  the marker from `(2,6)` to `(0,7)`; the second runs at the tunnel
  (sub-event 15) and removes it.  `Button1Click` cases `0x0e`/`0x0f` move
  the marker.  Handler: `touzoku` (`0x465900`), ported
  (`16-event-pilot.md` §8).
* **Zeno (floor 3, `(4,8)`, raw `0x07`).**  The player steps onto the
  trigger tile and stays there during the dialogue; the capture (`openning31`) then moves the player
  to the floor-2 prison at `(2,7)` with attack/defense reduced to 10/10
  (`17-static-findings.md` §1).  A post-Orb save corroborates the result
  (floor 3, `(10,4)`, Orb slot 0 = 1, tile `(4,8) = 0x06`).

## Event formations and NPC floors

The initial map contains several **multi-tile event formations**:

| Floor | Raw values | Positions | Likely meaning |
|---:|---|---|---|
| 15 | `0x61..0x69` | 3×3 block at (4..6,3..5) | multi-tile boss/event formation |
| 35 | `0x73..0x7b` | 3×3 block at (4..6,4..6) | Dragon/boss formation |
| 25 | `0x91` | (5,5) | Great Magic Master event |
| 44 | `0x9d` | (4,8) and (6,8) | illusion-floor event |
| 41–49 | `0x5d`, `0x85..0x9b` | scattered | final-area event triggers |

The `0x61..0x69` and `0x73..0x7b` blocks are notable because they are exactly
3×3; they are likely large bosses or scripted formations, not normal
single-tile monsters.

NPC floor tables decoded from the NPC switches:

| NPC | Floors |
|---|---|
| Merchant (`syounin`) | 2, 6, 7, 12, 15, 28, 31, 38, 39, 45, 47 |
| Old man (`roujin`) | 2, 3, 4, 6, 16, 18, 21, 23, 27, 31, 33, 36, 37, 39, 42, 45, 46, 48 |
| Altar | raw `0x10`, `handan` case `0x10` (first altar on floor 4) |
| Thief / wizards / vampire | event-specific, triggered by `0xb` sub-commands |

The old man on floor 3 is the Orb of the Hero source; the floor-2 old man
gives the first 3% attack/defense boost.

## Special-item slots

The 15 slots are at `0x4b86cc..0x4b8704`.  Slot 0 is separate from the map
pickups; slots 1..14 are filled by raw map values `0x25..0x32` in that order:

| Raw | Slot | Address |
|---:|---:|---:|
| — | 0 | `0x4b86cc` |
| `0x25` | 1 | `0x4b86d0` |
| `0x26` | 2 | `0x4b86d4` |
| `0x27` | 3 | `0x4b86d8` |
| `0x28` | 4 | `0x4b86dc` |
| `0x29` | 5 | `0x4b86e0` |
| `0x2a` | 6 | `0x4b86e4` |
| `0x2b` | 7 | `0x4b86e8` (quantity 3) |
| `0x2c` | 8 | `0x4b86ec` |
| `0x2d` | 9 | `0x4b86f0` |
| `0x2e` | 10 | `0x4b86f4` |
| `0x2f` | 11 | `0x4b86f8` |
| `0x30` | 12 | `0x4b86fc` |
| `0x31` | 13 | `0x4b8700` |
| `0x32` | 14 | `0x4b8704` |

### Slot → item mapping

The use handlers, the in-game text and the inventory icons (`itemdisp`:
Chtable sprite `0x23 + slot`) agree on this mapping; the use effects are
pinned by golden vectors (`16-event-pilot.md` §12d).

| Slot | Address | Item | Evidence |
|---:|---:|---|---|
| 0 | `0x4b86cc` | Orb of the Hero | old man on floor 3 sets it; save/forecast gate; handler `0x480f60` |
| 1 | `0x4b86d0` | Orb of Wisdom | handler `0x48198c` opens the large learned-information window |
| 2 | `0x4b86d4` | Orb of Flying | handler `0x481e80` checks adjacent stairs and enables flying; message `0x89` |
| 3 | `0x4b86d8` | Cross | passive; checked in battle against undead; no use handler |
| 4 | `0x4b86dc` | Magic Elixir | handler `0x48201c`; use message `0x9d`; life += attack×10 + defense×5 |
| 5 | `0x4b86e0` | Magic Mattock | handler `0x482128`; use message `0x9e` |
| 6 | `0x4b86e4` | Bomb (Destructible Ball) | handler `0x482234`; use message `0x9f` |
| 7 | `0x4b86e8` | Warp Staff ×3 | handler `0x482340`; use message `0xa0`; quantity 3 = charges; to the point mirror `0x78 − p` |
| 8 | `0x4b86ec` | Wings to fly up | handler `0x48244c`; use message `0xa1` |
| 9 | `0x4b86f0` | Wings to fly down | handler `0x482558`; use message `0xa2` |
| 10 | `0x4b86f4` | Dragon Slayer | passive; checked in battle against Dragon |
| 11 | `0x4b86f8` | Snow Crystal | handler `0x482664`; use message `0xa4`; turns the neighbouring lava into floor and is kept (the way to the floor-13 Sacred Sword) |
| 12 | `0x4b86fc` | Magic Key | handler `0x482770`; use message `0xa5` |
| 13 | `0x4b8700` | Super Magic Mattock | handler `0x48287c`; use message `0xa6` |
| 14 | `0x4b8704` | Lucky Gold | passive; doubles gold in battle |

Slot 0 holds 2 after the floor-3 old man (`roujin`) and 1 once the Orb
of Wisdom's talk upgrades it; the Orb of the Hero's window (`img6work`)
opens only for 1 (`item_img6work_none`).  Slot 7 counts the Warp Staff's
charges.  The handlers check slots 4..13 in the order of their use
messages `0x9d..0xa7`, which settles slot 6 (Bomb) against slot 7 (Warp
Staff).

### Effects

* **Orb of the Hero** (`img6work`, slot 0 = 1): up to seven distinct
  monsters of the floor in map order, each with `obudameiji`'s damage
  `(attack - defense) * (turns - 1)`, without the Cross or Dragon Slayer
  doubling (the remake doubles it, as the battle does; `08-remake.md`).
* **Orb of Flying** (`img4work`): only next to stairs.  Up/down
  (`Button8Click`/`Button9Click`) skips 44 (43 <-> 45), never above the
  highest floor reached or below 1; land (`Button10Click`) puts the hero
  on the floor's arrival cell for the last direction (on the highest floor,
  that of the adjacent stairs).
* **Orb of Wisdom** (`img5work`): the entries whose talked-to flag is 2
  (heard while holding it; the remake also lists flag 1, `08-remake.md`).
* **Magic Elixir** (case 5): `life += attack * 10 + defense * 5`; used up.
* **Magic Mattock**: the neighbouring raw-`0x08` walls (door kind 7);
  used up.  **Super Mattock**: every raw-`0x08` wall of the floor.
* **Bomb**: the neighbouring monsters (`mmmes`: `0x3d..0x5a`, `0x5d..0x60`,
  `0x85..0x90`, `0x93..0x9e`) dissolve and their gold is added, without the
  Lucky Gold; used up.
* **Warp Staff** (case 8): to the point mirror `0x78 - (y*11 + x)` if that
  cell is floor (`0x06`), else sprite `0x44`; a charge is spent either way.
* **Wings**: the same cell one floor up (not from 50) or down (not from 0)
  must be floor; no floor-limit update; used up.
* **Magic Key**: every yellow door (`0x01`) of the floor.
* **Snow Crystal** (`Button38Click` case 12): each neighbouring lava cell
  (`0x0d`) becomes floor (op `0x11`, then a door-style animation of kind
  9..12 by direction, `yukiwork`); the crystal is kept.
* **Cross / Dragon Slayer**: double the hero's attack against Zombie,
  Zombie Knight and Vampire / the Dragon.  **Lucky Gold**: battle gold
  times `slot14 + 1`.

> Message-number caveat: the `0x9d..0xa7` numbers above are **runtime
> item/use-list IDs**, not DFM string ordinals.  The DFM text extraction uses
> a different list/offset (see the text-ID note below).  Match by text
> content before treating equal-looking numbers as the same ID.

### Map placements (initial map)

Only five special items are on the initial map; the rest are placed by events
or given by NPCs:

| Raw | Slot | Floor | Position |
|---:|---:|---:|---|
| `0x25` | 1 | 5 | (3,8) |
| `0x26` | 2 | 1 | (1,10) |
| `0x2a` | 6 | 37 | (4,4) |
| `0x2c` | 8 | 45 | (1,9) |
| `0x30` | 12 | 46 | (2,7) |

The floor-1 orb of flying and floor-5 orb of wisdom are consistent with the
manual.  The remaining items are event/boss/merchant rewards.

## Equipment placements (initial map)

These confirm the floor indexing and the map/item decoding:

| Raw | Item | Floor | Position |
|---:|---|---:|---|
| `0x33` | Iron Sword | 5 | (10,10) |
| `0x34` | Silver Sword | 17 | (1,1) |
| `0x35` | Knight Sword | 33 | (9,9) |
| `0x36` | Holy Sword | 48 | (7,9) |
| `0x37` | Sacred Sword | 13 | (5,4) |
| `0x38` | Iron Shield | 9 | (8,6) |
| `0x39` | Silver Shield | 11 | (1,1) |
| `0x3a` | Knight Shield | 38 | (1,6) |
| `0x3b` | Holy Shield | 43 | (8,3) |
| `0x3c` | Sacred Shield | 44 | (5,5) |

The Sacred Shield on floor 44 matches the manual: floor 44 is an illusion and
the shield is hidden on a floor that “doesn’t exist”.  The Iron Sword on floor
5 and Iron Shield on floor 9 match the walkthrough.  The Silver Sword on 17
and Silver Shield on 11 match the NPC hint.

> **Sacred Sword note:** it is on **floor 13 at (5,4)**, but the floor-13
> map is mostly **lava** (`0x0d`).  The sword's alcove is surrounded by lava,
> so it is **not** reachable early.  It becomes reachable only after getting
> the **Snow Crystal** (slot 11) late in the game, which makes the lava
> passable.  The raw value `0x37` and the `+100` attack bonus are confirmed;
> the map placement is an early secret gated by a late-game item.

## Text-ID offset

The item/equipment pickup code uses **runtime pickup-message IDs** that are
offset from the **DFM string ordinals**.  Example: the Iron Shield case loads
runtime ID `0x9d`, while the extracted DFM list has “Got an Iron Shield…” at
ordinal `0x92`; the observed offset is `+0xb` for the ten sword/shield
messages.  This is a list-to-list offset, not a global rule: the special-item
use IDs (`0x9d..0xa7`) are in the runtime item/use list, and the DFM ordinal
of the same text may differ.

When extracting text, locate the message by content first, then apply/verify
the offset rather than assuming the DFM index equals the runtime ID.
