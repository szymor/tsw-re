# Game logic

## Input

The real key handler is `formkeydown` (`0x460bd8`).  It switches on the VCL
key code (`*param_3`):

| Key | Code | Action |
|---|---:|---|
| Enter | `0x0d` | interact / use selected item |
| Escape | `0x1b` | close message / cancel |
| Left | `0x25` | move left |
| Up | `0x26` | move up |
| Right | `0x27` | move right |
| Down | `0x28` | move down |

For arrows it sets the direction in `DAT_0048c590` (1=up, 2=left, 3=right,
4=down), computes the target map index in `DAT_0048c584`, and calls `handan`
(`0x444490`).  The mouse arrow buttons use `timer3ontimer` (`0x461760`) to
repeat the same movement while the button is held.

`DAT_0048c5c6` is a mouse-button/mode flag; `DAT_0048c5ce` toggles the
inventory/message overlay.

## Movement and tile actions (`handan`, `0x444490`)

`handan` reads the target tile:

```
raw = mapBase[floor*0x7b + targetIndex]
```

and dispatches by value.

### Doors: raw 1/2/3

```
raw 1 (yellow door): if yellowKeys > 0: yellowKeys--, openAnim=4
raw 2 (blue door):   if blueKeys   > 0: blueKeys--,   openAnim=5
raw 3 (red door):    if redKeys    > 0: redKeys--,    openAnim=6
```

The player **stays in front of the door** (`handan` `0x4445d3` queues
`{2, sprite}`, `{8}`, the door command `{0x0d, target, raw}` expanded by
`doorstackin`, `{0}`, `{1, 1}`; it never calls `idou`).  The remake used
to step onto the opened door; the differential test below caught it.

If a key was consumed, it starts the door animation (`openAnim` is an
open/animation index, not directly the map byte) and the door animation
updates the map tile.  Comparing the initial map with later original saves
shows that opened doors, picked-up keys, items and defeated monsters all end
up as raw `0x06` (ordinary floor) in the saved map block.  The Phase 1
implementation uses that final state; the intermediate door-frame sprites
`0x04`/`0x05` are only relevant to animation.

The relevant globals are:

| Global | Meaning |
|---|---|
| `0x4b86a8` | yellow keys |
| `0x4b86ac` | red keys |
| `0x4b86b0` | blue keys |
| `0x48c58c` | current animation sprite |

The colors were confirmed against the tile atlas: sprite 0 is the brown/yellow
door, sprite 1 is blue, sprite 2 is red; the key sprites are 28=yellow,
29=red, 30=blue.

The key-display routine `keydisplay` (`0x44bed8`) draws 24 cells in a 6×4
grid from the dedicated 32×32 `Key.oz` atlas, not from `Chtable`.  `Key.oz`
contains four 16×16 cells in the order red (0), blue (1), yellow (2), empty
(3).  The loop fills cells with red keys first, then blue, then yellow, then
empty cells, so the inventory order is **red → blue → yellow → empty**.

### Floor / move: raw 6

Calls `idou` (`0x44c04c`), which:

* updates player x/y from the target index,
* plays the step animation,
* draws the player sprite,
* checks adjacent special tiles and hidden passages,
* calls `disp` to refresh the stat panel.

The player frames are adjacent Chtable sprite pairs selected by
`DAT_004b87e8` (facing): 1=down uses 184/185, 2=left 192/193, 3=right
200/201, 4=up 208/209.  The first sprite is the colored player; the second
is the black-on-white transparency mask.  The conversion script composites
them into `data/gfx/Player.png` with frames ordered down, left, right, up.

The player starts at `(5,10)`; the map is 11×11.

### Case 7: special map action

The `handan` switch also has case 7, which calls `mevent` (`0x46397c`).
`mevent` is a map-event helper, **not** the normal stairs transition (those
are raw `0x0b`/`0x0c`, below).  Raw `0x07` looks like a wall sprite but is a
bump trigger: on floor 3 the tile at `(4,8)` (index 92) starts the Zeno
opening, and on the `ichicheck` floors it starts that floor's event.  Raw
`0x09` is the pressable secret wall (floor 5 `(8,10)` beside the Iron Sword,
floor 9 `(9,4)`), `handan` case 9, which shows “The wall collapsed!”.  Both
are ported; see `10-events-and-items.md` and `16-event-pilot.md`.

### Stairs: raw 0x0b / 0x0c

The floor transition is handled by `handan` when the player steps onto a
stairs tile:

```
raw 0x0b (up):   if floor == 43: floor = 45 else floor++
                 floor_limit = max(floor_limit, floor)
                 x,y = stairTableUp[floor]       # first gap byte
raw 0x0c (down): if floor == 45: floor = 43 else floor--
                 x,y = stairTableDown[floor]     # second gap byte
```

The check is on the **source** floor: 43 jumps to 45, 45 jumps back to 43.
`floor_limit` (`0x4b869c`) is the highest floor ever reached.  Floor 44 is an
illusion and is skipped.  Floor 50 is not reachable in normal play; the final
boss is on floor 49.

The separate `timer3ontimer` cases 5/6 implement the **orb of flying** travel
between already-visited floors and are bounded by `floor_limit`.

The player's arrival tile after a transition is read from the floor's 2-byte
stair table: first byte when going up, second byte when going down (see
`02-map-format.md`).

### Case 9: pressable `0x09` secret wall

The `handan` switch case 9 (`0x4447df`) handles the brown raw-`0x09` secret
wall.  It calls `timer2on`, sets `g_anim_sprite = 1`, copies `ListBox2[0x89]`
(“The wall collapsed!”, `18-messages.md`) to the status-bar string
`0x4b8684` (shown in `Edit8` by `disp`), and queues `{0}`, `{0x02, 1}`,
`{0}`, the door command `{0x0d, target, 7}` expanded by `doorstackin`, then
`{0}`, `{0x01, 1}`.  The wall slides down like the prison cell door and
`dooropen` writes the floor tile; **the player does not move**.  Ported as
`secret_wall` in `src/events/builders.lua` (vectors `handan_secret_wall_*`).  The initial map places these walls on floor 5 `(8,10)` beside the
Iron Sword and floor 9 `(9,4)`.  Normal map items and equipment are the raw
ranges `0x1d..0x3c`, handled below.

### Case 10: fixed floor event

Handles hard-coded floor/position events (for example floor 12 position 10,
floor 16 position 0x78, floor 19 position 0x1b).  These are boss/guard/
treasure events; see `09-open-questions.md`.

## Consumables and keys

| Raw | Effect |
|---:|---|
| `0x1d` | +1 yellow key (atlas sprite 28) |
| `0x1e` | +1 red key (atlas sprite 29) |
| `0x1f` | +1 blue key (atlas sprite 30) |
| `0x20` | life += `((floor+9)/10) * 50 * (difficulty+1)` |
| `0x21` | life += `((floor+9)/10) * 200 * (difficulty+1)` |
| `0x22` | attack += `((floor+9)/10) * (difficulty+1)` |
| `0x23` | defense += `((floor+9)/10) * (difficulty+1)` |

`difficulty` is `DAT_004b8904` (0 normal, 1 hard-like multiplier).
The area factor `(floor+9)/10` makes items stronger on deeper floors.

## Equipment

### Swords (`0x4b86c4`, flags `0x4b8708..0x4b8718`)

| Raw | Item | Bonus | Display level | Flag |
|---:|---|---:|---:|---:|
| `0x33` | Iron Sword | +10 × (diff+1) | 1 | `0x4b8708` |
| `0x34` | Silver Sword | +20 × (diff+1) | 2 | `0x4b870c` |
| `0x35` | Knight Sword | +40 × (diff+1) | 3 | `0x4b8710` |
| `0x36` | Holy Sword | +50 × (diff+1) | 4 | `0x4b8714` |
| `0x37` | Sacred Sword | +100 × (diff+1) | 5 | `0x4b8718` |

### Shields (`0x4b86c8`, flags `0x4b871c..0x4b872c`)

| Raw | Item | Bonus | Display level | Flag |
|---:|---|---:|---:|---:|
| `0x38` | Iron Shield | +10 × (diff+1) | 1 | `0x4b871c` |
| `0x39` | Silver Shield | +20 × (diff+1) | 2 | `0x4b8720` |
| `0x3a` | Knight Shield | +40 × (diff+1) | 3 | `0x4b8724` |
| `0x3b` | Holy Shield | +50 × (diff+1) | 4 | `0x4b8728` |
| `0x3c` | Sacred Shield | +100 × (diff+1) | 5 | `0x4b872c` |

The pickup adds the bonus to `attack` / `defense` and sets the display level
and the flag.  The initializer sets display level 5 and the Sacred flags to 1
but does **not** add the bonuses; see the quirk in `03-state-and-save.md`.

## Special items (15 slots)

Slots are at `0x4b86cc` (slot 0) through `0x4b8704` (slot 14).  The map
pickups `0x25..0x32` fill slots 1..14; slot 0 (the orb of the hero) is given
by the old man on floor 3.  The map-to-slot mapping is:

| Raw | Slot | Verified Chtable icon |
|---:|---:|---:|
| `0x25` | 1 | `0x24` |
| `0x26` | 2 | `0x25` |
| `0x27` | 3 | `0x26` |
| `0x28` | 4 | `0x27` |
| `0x29` | 5 | `0x28` |
| `0x2a` | 6 | `0x29` |
| `0x2b` | 7 | `0x2a` (quantity 3) |
| `0x2c` | 8 | `0x2b` |
| `0x2d` | 9 | `0x2c` |
| `0x2e` | 10 | `0x2d` |
| `0x2f` | 11 | `0x2e` |
| `0x30` | 12 | `0x2f` |
| `0x31` | 13 | `0x30` |
| `0x32` | 14 | `0x31` |

`itemdisp` draws inventory slot `s` from Chtable sprite `0x23 + s`.  The
older icon values `0x16`/`0x15`/`0x28…` in earlier notes used a different
numbering and are superseded by this verified mapping.

The full 15-slot mapping is in `10-events-and-items.md`: slot 0 Orb of the
Hero, 1 Orb of Wisdom, 2 Orb of Flying, 3 Cross, 4 Magic Elixir, 5 Magic
Mattock, 6 Bomb, 7 Warp Staff ×3, 8/9 flying wings, 10 Dragon Slayer,
11 Snow Crystal, 12 Magic Key, 13 Super Mattock, 14 Lucky Gold.

The **gold multiplier** in the battle reward uses slot 14 (`0x4b8704`), which
matches the Lucky Gold item.  The charge semantics of slot 7 (×3) still need
confirmation in the PoC.

## Battle (`taisen`, `0x44a480`)

Normal monsters are map raw `0x3d..0x5a`, which map to monster table indices
`0..14`:

```
index = ((raw + 1) >> 1) - 31
```

The monster table is at `0x489910`, 33 records of 16 bytes:

```
int32 hp
int32 attack       # "power"
int32 defense      # "strength"
int32 gold
```

The battle outcome is simulated exactly by `FUN_00450064` (`0x450064`):

```
hp = monsterHP * (difficulty + 1)
multiplier = 1 or 2 (see below)
if monsterDefense*(difficulty+1) >= playerAttack*multiplier:
    unwinnable; DAT_0048c564 = 1; return
turns = 0
while hp > 0:
    hp += monsterDefense * (difficulty + 1) - playerAttack * multiplier
    turns++
damageToPlayer = max(0, monsterAttack*(difficulty+1) - playerDefense) * (turns - 1)
```

* `multiplier` is `1` normally.
* It becomes `2` against Zombie (12), Zombie Knight (13) and Vampire (17)
  when the Cross (slot 3) is owned.
* It becomes `2` against Dragon (19) when the Dragon Slayer (slot 10) is
  owned.
* The player attacks first; the monster gets `turns - 1` attacks.
* If the monster cannot be damaged, `DAT_0048c564 = 1`.
* Victory awards `monsterGold * (LuckyGold + 1)`; death calls `gameover`.

The turn recursion is `FUN_0045031c` (normal) and `FUN_00450360` (doubled).
The monster info window (`monshyouji`) and the Hero Orb forecast use the same
simulation.

## NPCs and events

The main event functions are:

| Address | Name | Role |
|---:|---|---|
| `0x46397c` | `mevent` | map-event helper (not the normal stairs transition) |
| `0x46cb1c` | `ichicheck` | per-floor event dispatcher (floors 10..49) |
| `0x449014` | `syounin` | merchant |
| `0x449e18` | `zyouout` | altar |
| `0x449708` | `roujin` | old man |
| `0x465900` | `touzoku` | thief |
| `0x44c464` | `madoushi1` | wizard |
| `0x462560` | `madoushi2` | wizard variant |
| `0x47c978` | `banpaiawork` | vampire |
| `0x478f0c` | `yousei` | fairy / area boss / ranking |
| `0x454da0` | `gameover` | death / new game reset |

`Timer1Timer` (`0x443120`) checks two hard-coded special events:

* floor 15, map index `0x3c`, tile `0x4b90a7 == 6`, flag `0x4b88e8 < 2`
  → `FUN_00454a58` and increment `0x4b88e8`.
* floor 35, map index `0x47`, tile `0x4b9a4e == 6`, flag `0x4b88ec < 2`
  → `FUN_004548a0` and increment `0x4b88ec`.

These are the two “secret tunnel / dragon” events described in the game text.

## Area bosses and floors

`yousei` handles the five ranking/checkpoint floors:

```
floor 10  -> option1.dat
floor 20  -> option2.dat
floor 25  -> option3.dat
floor 40  -> option4.dat
floor 49  -> option5.dat
```

At these floors it computes the fairy score, updates the ranking table,
writes `save0.dat` as a checkpoint, and plays the fairy/boss sequence.

Floors 43 and 45 are special in the stairs logic: floor 43 jumps to 45 and
floor 45 jumps back to 43, skipping the illusion floor 44.  Floor 50 is not
normally reachable.
