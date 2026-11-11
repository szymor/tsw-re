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

The interpreter is `stackwork` (`0x4526c0`).  Its main switch gives the
following opcode map.  Opcode `0` is the common frame/delay/sync step; its
exact timing role is the least certain entry.

| Opcode | Meaning | Arguments |
|---:|---|---|
| `0` | frame no-op / one stackwork invocation | — |
| `1` | sound/music | arg1 selects the sound/BGM index |
| `2` | set sprite | arg1 = sprite |
| `3` | altar/stat event | draws the player and calls `zyouout` (altar) |
| `4` | apply signed player-life delta | `life += defense - monsterAttack`, clamped at 0 |
| `5` | apply signed monster-HP delta | `hp += monsterDefense - playerAttack*multiplier`, cross/Dragon checks |
| `6` | set map tile | arg1 = map index, arg2 = new tile |
| `7` | add gold | `arg1 * (LuckyGold + 1)` |
| `8` | show item/key UI | — |
| `9` | three-sprite animation | uses current and previous command arguments |
| `10` | start sub-event | arg1 + 10 |
| `0xb` | sub-command dispatch | see table below |
| `0xc` | monster movement (`monidou`) | — |
| `0xd` | door animation (`dooropen`) | — |
| `0xe` | battle hit animation + monster damage | uses current/previous sprite arguments |
| `0xf` | stop BGM | — |
| `0x10` | add life | arg1 |
| `0x11` | silent map-tile set | arg1 = index, arg2 = tile |
| `0x12` | no-op | same default path as opcode `0` |
| `0x13` | halve life | — |
| `0x14` | set state field | arg1 = direct state index, arg2 = value |
| `0x15` | draw actor at map index | arg1 = mode, arg2 = map index |
| `0x16` | reset/new game/ending | — |

Opcode `0` is a one-invocation no-op; `timer2timer` (`0x45265c`) executes up
to two stackwork invocations per timer event.  `0x12` shares the default path
with `0`.  Opcode `0x14` indexes `0x4b8688` directly, so its state indices are
the `GameState` indices.  The full structural reference is
[`12-event-vm.md`](12-event-vm.md).

Sub-command `0xb`:

| arg1 | Action |
|---:|---|
| `1` | `openning31` (`0x4653b8`) |
| `2` | `zenolivework` (`0x461a0c`) + map tile change |
| `3` | `madoushi2` (`0x462560`) |
| `4` | `banpaiawork` (`0x47c978`) + animation |
| `5` | `moncheck` (`0x4664e4`) |
| `6` | game-over/ending; sets difficulty flags |
| `7` | title screen (`title05.bmp`) |
| `8` | player/map update (`0x443648`) |
| `9` | player animation (`0x4433a4`) |
| `10` | `taisen2` (`0x47f7d4`) |
| `0xb` | change floor to arg2; floor 50 special handling |
| `0xc` | `openning9` (`0x482e60`) |
| `0xd` | hide message/UI |
| `0xe` | hide all event images |
| `0xf` | screen fade/transition |

The event system is therefore a compact **timeline VM**.  A re-implementation
can either implement these opcodes directly or translate the timelines into a
higher-level script format.

Sub-commands `1`, `2`, `3`, `5` and `10` decrement the command count before
calling their handler and increment it afterwards.  The sub-handler's appended
commands therefore overwrite the current `0xb` command and become the next
command(s); this is a cursor splice, not a nested call stack.  Sub-command `6`
uses the same splice around the game-over path.

## `handan` switch cases 7 / 9 / 10

The tile-action switch is on the raw map value, but cases 7, 9 and 10 overlap
values used by wall/floor sprites.  Evidence:

* case 9 loads runtime message `0x89` and marks the target tile as floor.
  Using the observed map/equipment offset (`DFM ordinal = runtime ID - 0x0b`),
  `0x89 - 0x0b = 0x7e` (ordinal 126), “The wall collapsed!”.  The earlier
  orb-of-flying interpretation compared equal-looking numbers from different
  lists and was wrong.  The initial map places raw `0x09` at floor 5 `(8,10)`
  (the secret tunnel beside the Iron Sword) and floor 9 `(9,4)`.
* cases 7 and 10 are reached from event code that sets up a synthetic target
  index; they are not normal walk-on tiles.

Concrete case-9 evidence: the case is in `handan` at `0x444803`; it loads
runtime message `0x89`, marks the target tile as floor, and calls the pickup
path.  The event call site that supplies the synthetic target is still to be
enumerated.

Conclusion: cases 7/9/10 are **scripted action classes** invoked by the event
system with a synthetic target tile.  Normal movement is normally blocked
before the switch for wall tiles, with one confirmed exception: the floor-3
gray tile at `(4,8)` (raw `0x07`, index 92).  Bumping it calls `mevent`; the
floor-3 case `0x463a2c` clears the tile to `0x06` and runs the
`zenolivework`/sub-event sequence while the player remains in place.  The
earlier open/teleport interpretation was wrong; the one-shot mechanism is the
tile mutation, not a separate flag.  The pressable raw-`0x09`
walls (floor 5 `(8,10)` and floor 9 `(9,4)`) use `handan` case 9 and do show
“The wall collapsed!” before opening.  See `16-event-pilot.md`.

## Static append-site inventory

The extractor (`tools/extract_timelines.py`) inventories the static command
append sites in each handler address window.  It **does not follow branches
or shared control-flow tails**, so the tables below are scopes, not final
executable timelines.  Current inventory:

| Floor | Append sites | Character |
|---:|---:|---|
| 10 | 312 | long area-1 sequence: 54 monster-move commands (`0xc`), 4 map changes, 2 sub-events |
| 15 | 5 | short: music + door animation |
| 20 | 77 | 32 × sub-command `0xb/4` (`banpaiawork`) — vampire sequence |
| 24 | 236 | 17 actor draws, 27 sub-commands (`0xb/10` and `0xb/11`) — multi-floor cutscene |
| 25 | 7 | short boss trigger |
| 30 | 8 | short |
| 32 | 78 | 13 monster moves, map change index `9 → 0x85` |
| 33 | 16 | short |
| 38 | 8 | short |
| 40 | 253 | 88 monster moves, 14 sub-commands (`0xb/5`, `0xb/10`), 3 map changes |
| 42 | 57 | 6 map changes, 1 actor draw, 4 sub-events |
| 43 | 29 | 2 monster moves |
| 48 | 6 | map change index `0 → 0x5d` |
| 49 | 18 | sub-commands `0xb/3` and `0xb/2` — final sequence |

`opcode 0` is the most common command; it is the frame/delay/sync step.
`opcode 1` is sound/music, `0xc` is monster movement, and `0xb` is the
NPC/boss/title/floor-change sub-dispatch.

The counts are **address-window inventory counts** from the jump-table
targets; shared control-flow tails and conditional branches can move append
sites between adjacent cases.  The tool reports incomplete sites separately.
The full inventory is generated locally into `append_sites.txt`; it is not
committed.

## Reimplementation status

The Phase 3 Lua runtime (`src/events.c`, scripts in `src/events/events.lua`)
implements coroutine message/choice/tick waits plus state/map/gold/battle/
checkpoint helpers.  Old-man dialogues for floors 2/3/4/6 are confirmed against
the original `roujin`/live game; the floor-2 flag is confirmed at `0x62`, while
the floor-3/4/6 state flags remain provisional.  Merchant offers are initial
approximations, altars are not implemented, and the floor-10 Golden
Knight/fairy sequence uses the confirmed state flag `0x6a` (`0x4b8830`).
The opening escape sequence's actor/Zeno/prisoner support is implemented; the
one-time launch `open0.bmp` path and exact actor timing remain open.  The full
original flag map and
the 312-site floor-10 timeline still require differential validation.

## Opening/prisoner event trace (partial)

A live original trace confirmed that floor-2 raw `0x15` at `(2,6)` is the
prisoner old-man story trigger.  It is **not** an altar.

The first interaction leaves the player at the source tile and shows the
“What a relief you're okay! …” dialogue.  The `0x15` marker then moves from
`(2,6)` to `(0,7)`.  Approaching `(0,7)` shows the second “O.K. Well, excuse
my going first …” dialogue; afterwards the marker disappears.  This is a moving
event actor encoded on the map, not a static NPC tile.

The Zeno/Magic-Sergeant event is triggered by entering floor-3 tile `(4,8)`.
The player stays on the trigger tile during the dialogue; the event then
changes that tile from raw `0x07` to `0x06` and teleports the player to the
floor-2 prison.  A post-Orb save confirms floor 3, player `(10,4)`, Orb slot 0
= 1, nerfed attack/defense 10/10, and tile `(4,8)=0x06`.  The Lua runtime now
has initial actor and image-cutscene support; Zeno’s four Magic Sergeants and
the prisoner sequence are implemented.  Actor positions/timing are approximate,
and the one-time launch `open0.bmp` sequence remains to be implemented.

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
| Altar (`zyouout`) | generic handler; called by event scripts (first altar is on floor 4) |
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
| 11 | `0x4b86f8` | Snow Crystal | handler `0x482664`; use message `0xa4`; makes the lava on floor 13 passable |
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
