# Data tables

## Monster table

Runtime address `0x489910`, 33 records × 16 bytes.  Each record is four
little-endian int32:

```
[0] hp
[1] attack / "power"
[2] defense / "strength"
[3] gold reward
```

The names come from the game's own monster-name list and align 1:1 with the
table order (index 0 = Green Slime).  Values are the retail v1.2 values.

| # | Name | HP | Atk | Def | Gold |
|---:|---|---:|---:|---:|---:|
| 0 | Green Slime | 35 | 18 | 1 | 1 |
| 1 | Red Slime | 45 | 20 | 2 | 2 |
| 2 | Big Slime | 130 | 60 | 3 | 8 |
| 3 | Bat | 35 | 38 | 3 | 3 |
| 4 | Priest | 60 | 32 | 8 | 5 |
| 5 | Superion Priest | 100 | 95 | 30 | 22 |
| 6 | Gate-Keeper C | 50 | 48 | 22 | 12 |
| 7 | Gate-Keeper B | 100 | 180 | 110 | 100 |
| 8 | Skeleton C | 50 | 42 | 6 | 6 |
| 9 | Skeleton B | 55 | 52 | 12 | 8 |
| 10 | Skeleton A | 100 | 65 | 15 | 30 |
| 11 | Big Bat | 60 | 100 | 8 | 12 |
| 12 | Zombie | 260 | 85 | 5 | 18 |
| 13 | Zombie Knight | 320 | 120 | 15 | 30 |
| 14 | Rock | 20 | 100 | 68 | 28 |
| 15 | The Magic Sergeant, Zeno | 8000 | 5000 | 1000 | 500 |
| 16 | The Magic Sergeant | 230 | 450 | 100 | 100 |
| 17 | Vampire | 444 | 199 | 66 | 144 |
| 18 | Giant Octopus | 1200 | 180 | 20 | 100 |
| 19 | Dragon | 1500 | 600 | 250 | 800 |
| 20 | Golden Knight | 120 | 150 | 50 | 100 |
| 21 | Knight | 160 | 230 | 105 | 65 |
| 22 | Swordsman | 100 | 680 | 50 | 55 |
| 23 | Soldier | 210 | 200 | 65 | 45 |
| 24 | Ghost Soldier | 220 | 180 | 30 | 35 |
| 25 | Slime Man | 320 | 140 | 20 | 30 |
| 26 | Great Magic Master | 4500 | 560 | 310 | 1000 |
| 27 | Vampire Bat | 200 | 390 | 90 | 50 |
| 28 | SlimeLord | 360 | 310 | 20 | 40 |
| 29 | Magician A | 200 | 380 | 130 | 90 |
| 30 | Magician B | 220 | 370 | 110 | 80 |
| 31 | Dark Knight | 180 | 430 | 210 | 120 |
| 32 | Gate-Keeper A | 180 | 460 | 360 | 200 |

### Normal encounter mapping

Map raw values `0x3d..0x5a` (61..90) select table indices 0..14:

```
index = ((raw + 1) >> 1) - 31
```

Higher table indices (bosses and special entities) are used by event code,
not by the normal map encounter path.

### Battle formulas

The battle outcome is simulated exactly by `FUN_00450064` (`0x450064`):

```
hp = monsterHP * (difficulty + 1)
turns = 0
while hp > 0:
    hp += monsterDefense * (difficulty + 1) - playerAttack * multiplier
    turns++
damageToPlayer = max(0, monsterAttack*(difficulty+1) - playerDefense) * (turns - 1)
```

* `multiplier` is normally `1`.
* Against Zombie (index 12), Zombie Knight (13) and Vampire (17), if the
  Cross (slot 3) is owned, `multiplier = 2`.
* Against Dragon (index 19), if the Dragon Slayer (slot 10) is owned,
  `multiplier = 2`.
* The player attacks first; the monster gets `turns - 1` attacks.
* If `monsterDefense*(difficulty+1) >= playerAttack*multiplier`, the monster
  cannot be killed (`DAT_0048c564 = 1`).

The turn recursion is `FUN_0045031c` (`0x45031c`) for normal attacks and
`FUN_00450360` (`0x450360`) for doubled attacks.  `DAT_0048c56c` is the turn
count and `DAT_0048c568` the predicted total damage.  Victory awards
`monsterGold * (LuckyGold + 1)`.

The monster info window and the Hero Orb forecast use the same simulation.

The observed battle code also multiplies some values by
`(state[0x27c] + 1)` (option flag `0x4b8904`) and by
`(specialItem[14] + 1)` (`0x4b8704`).  The first is the difficulty/stat
multiplier; the second is a gold/reward multiplier tied to the last special
item slot.  Confirm both in the PoC before balancing.

### Field-order verification

`FUN_00450064` (`0x450064`) uses the table as follows:

```
+0x00  DAT_00489910  -> monster HP (stored in DAT_0048c5b4)
+0x04  DAT_00489914  -> monster attack; compared with player defense
+0x08  DAT_00489918  -> monster defense; compared with player attack
+0x0c  DAT_0048991c  -> reward/gold value
```

This confirms the column order `[HP, attack, defense, gold]`.  The table
occupies `0x489910..0x489b1f` (33 × 16 bytes).  The 121-byte render-order
table starts at `0x489b20` (the code uses `0x489b1f + i`, `i=1..121`).

## Key and door mapping

| Entity | Raw | Atlas sprite | Count global |
|---|---:|---:|---|
| Yellow key | `0x1d` | 28 | `0x4b86a8` |
| Red key | `0x1e` | 29 | `0x4b86ac` |
| Blue key | `0x1f` | 30 | `0x4b86b0` |
| Yellow door | `0x01` | 0 | consumes `0x4b86a8` |
| Blue door | `0x02` | 1 | consumes `0x4b86b0` |
| Red door | `0x03` | 2 | consumes `0x4b86ac` |

Open-door sprites are 3/4/5 (raw 4/5/6).  The key display (`keydisplay`)
draws up to 24 keys in a 6×4 grid.

## Equipment table

Swords (`0x33..0x37`) add attack; shields (`0x38..0x3c`) add defense.  The
bonus is `base * (optionFlag 0x4b8904 + 1)`.

| Raw | Item | Bonus | Level | Flag |
|---:|---|---:|---:|---:|
| `0x33` | Iron Sword | +10 | 1 | `0x4b8708` |
| `0x34` | Silver Sword | +20 | 2 | `0x4b870c` |
| `0x35` | Knight Sword | +40 | 3 | `0x4b8710` |
| `0x36` | Holy Sword | +50 | 4 | `0x4b8714` |
| `0x37` | Sacred Sword | +100 | 5 | `0x4b8718` |
| `0x38` | Iron Shield | +10 | 1 | `0x4b871c` |
| `0x39` | Silver Shield | +20 | 2 | `0x4b8720` |
| `0x3a` | Knight Shield | +40 | 3 | `0x4b8724` |
| `0x3b` | Holy Shield | +50 | 4 | `0x4b8728` |
| `0x3c` | Sacred Shield | +100 | 5 | `0x4b872c` |

## Consumables

| Raw | Effect |
|---:|---|
| `0x20` | life += `((floor+9)/10) * 50 * (diff+1)` |
| `0x21` | life += `((floor+9)/10) * 200 * (diff+1)` |
| `0x22` | attack += `((floor+9)/10) * (diff+1)` |
| `0x23` | defense += `((floor+9)/10) * (diff+1)` |

## Special item slots

15 slots at `0x4b86cc..0x4b8704`.  Slot 0 is the orb of the hero (old man,
floor 3).  Slots 1..14 are filled by map pickups `0x25..0x32`.

| Raw | Slot | Icon |
|---:|---:|---:|
| `0x25` | 1 | `0x16` |
| `0x26` | 2 | `0x15` |
| `0x27` | 3 | `0x28` |
| `0x28` | 4 | `0x29` |
| `0x29` | 5 | `0x2a` |
| `0x2a` | 6 | `0x2b` |
| `0x2b` | 7 | `0x2c` (quantity 3) |
| `0x2c` | 8 | `0x2d` |
| `0x2d` | 9 | `0x2e` |
| `0x2e` | 10 | `0x2f` |
| `0x2f` | 11 | `0x30` |
| `0x30` | 12 | `0x31` |
| `0x31` | 13 | `0x32` |
| `0x32` | 14 | `0x33` |

Known item names in the game text: Orb of the Hero, Orb of Wisdom, Orb of
Flying, Magic Mattock, Super Magic Mattock, Magic Elixir, Cross, Snow
Crystal, Warp Staff, Bomb.  The corrected slot-to-name mapping (including
the slot 6/7 Warp Staff/Bomb ambiguity) is in
`10-events-and-items.md`.

## Event flags (selected)

| Address | Meaning |
|---:|---|
| `0x4b87e8` | facing/step animation frame (1..4) |
| `0x4b88e8` | floor-15 secret event counter (max 2) |
| `0x4b88ec` | floor-35 secret event counter (max 2) |
| `0x4b88fc` | save-format magic (`-4`) |
| `0x4b8904` | difficulty/stat multiplier option |
| `0x4b8908` | item/stat carry-over option |
| `0x4b8918` / `0x4b891c` | state checksums |
| `0x4b8930` | save format version (12) |

The large `0x4b8730..0x4b88fb` region stores per-event/per-treasure flags.
`syokidata` zeroes it on a full new game.  The exact flag-to-event mapping is
still being decoded; see `09-open-questions.md`.
