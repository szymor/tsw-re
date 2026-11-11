# Events and special items

This is the first pass at the two highest-risk open areas: the per-floor event
dispatcher and the 15 special-item slots.  It contains confirmed facts plus
clearly marked hypotheses.

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
commands to an array:

```
DAT_0048c5ac                  command count
DAT_0048c74c + i*6 + 0        opcode (u16)
DAT_0048c74c + i*6 + 2        arg1   (u16)
DAT_0048c74c + i*6 + 4        arg2   (u16)
```

The generic processors (`FUN_0044bc0c`, `FUN_004660d0`, `FUN_0044c9f0`,
`stackwork`, etc.) consume the list.  Observed opcodes include:

| Opcode | Observed use |
|---:|---|
| `0` | sync/delay/clear |
| `1` | end/wait for input |
| `2` | draw sprite/animation (arg1 = sprite) |
| `6` | set sprite/animation (arg1 = sprite) |
| `8` | clear/next frame |
| `0xb`–`0xd` | wait/message/sprite timing (partial) |

The exact opcode semantics are still being decoded.  The important result is
that the event system is **data/timeline-driven**, not a set of hard-coded
gameplay branches.  A re-implementation can use a small timeline interpreter
with the same opcodes, or translate the timelines into a higher-level script
format.

## `handan` switch cases 7 / 9 / 10

The tile-action switch is on the raw map value, but cases 7, 9 and 10 overlap
values used by wall/floor sprites.  Evidence:

* case 9 loads message `0x89` and marks the target tile as floor; in the
  extracted text list index `0x89` is the **orb of flying** pickup message,
  so this is very likely the scripted orb-of-flying pickup.
* cases 7 and 10 are reached from event code that sets up a synthetic target
  index; they are not normal walk-on tiles.

Conclusion: cases 7/9/10 are **scripted action classes** invoked by the event
system with a synthetic target tile.  Normal movement is blocked before the
switch for wall tiles.  This resolves the “dead code” concern; the remaining
work is to list each scripted call site.

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

The following mapping is strongly supported by the use handlers and the
in-game text.  Items marked “hypothesis” need icon/atlas confirmation.

| Slot | Address | Item | Evidence |
|---:|---:|---|---|
| 0 | `0x4b86cc` | Orb of the Hero | old man on floor 3 sets it; save/forecast gate; handler `0x480f60` |
| 1 | `0x4b86d0` | Orb of Wisdom | handler `0x48198c` opens the large learned-information window |
| 2 | `0x4b86d4` | Orb of Flying | handler `0x481e80` checks adjacent stairs and enables flying; message `0x89` |
| 3 | `0x4b86d8` | Cross | passive; checked in battle against undead; no use handler |
| 4 | `0x4b86dc` | Magic Elixir | handler `0x48201c`; use message `0x9d` |
| 5 | `0x4b86e0` | Magic Mattock | handler `0x482128`; use message `0x9e` |
| 6 | `0x4b86e4` | Bomb (Destructible Ball) | handler `0x482234`; use message `0x9f` |
| 7 | `0x4b86e8` | Warp Staff ×3 | handler `0x482340`; use message `0xa0`; quantity 3 = charges |
| 8 | `0x4b86ec` | Wings to fly up | handler `0x48244c`; use message `0xa1` |
| 9 | `0x4b86f0` | Wings to fly down | handler `0x482558`; use message `0xa2` |
| 10 | `0x4b86f4` | Dragon Slayer | passive; checked in battle against Dragon |
| 11 | `0x4b86f8` | Snow Crystal | handler `0x482664`; use message `0xa4` |
| 12 | `0x4b86fc` | Magic Key | handler `0x482770`; use message `0xa5` |
| 13 | `0x4b8700` | Super Magic Mattock | handler `0x48287c`; use message `0xa6` |
| 14 | `0x4b8704` | Lucky Gold | passive; doubles gold in battle |

> Slot 0 state quirk: the old man on floor 3 sets `0x4b86cc = 2`, while the
> use handler `0x480f60` checks for `1`.  Value `2` probably means “owned but
> not yet activated” or is a separate event flag; confirm in the orb-of-hero
> use path.

The slot 6/7 order is resolved by two independent orderings:

* The handlers check slots 4→13 in order and use icon indices 5→14.
* The item-use messages appear in the same order:
  `0x9d` elixir, `0x9e` mattock, `0x9f` bomb, `0xa0` warp staff,
  `0xa1` wings-up, `0xa2` wings-down, `0xa3` dragon slayer,
  `0xa4` snow crystal, `0xa5` magic key, `0xa6` super mattock,
  `0xa7` lucky gold.

Therefore slot 6 is the Bomb and slot 7 is the Warp Staff.  The value `3` in
slot 7 is most likely **warp-staff charges** (the item is not listed as
unlimited in the manual; the unlimited item is the Orb of Flying).  This
should still be confirmed by using the warp staff repeatedly in the original.

The remaining verification is the inventory-icon atlas: the icon indices
`5..14` should show elixir, mattock, bomb, staff, wings-up, wings-down,
dragon-slayer, snow crystal, key, super-mattock in that order.

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

## Text-ID offset

The item/equipment pickup code uses string-list IDs that are offset from the
DFM string order.  Example: the Iron Shield case loads resource `0x9d`, while
the extracted DFM list has “Got an Iron Shield…” at index `0x92`; the offset
is `+0xb`.  The same `+0xb` offset holds for the ten sword/shield messages.
When extracting text, locate the message by content first, then apply/verify
the offset rather than assuming the DFM index equals the resource ID.
