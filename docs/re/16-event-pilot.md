# P3 event pilot (first pass)

Status: **P3 pilot extraction started**.  This document records the first
CFG-aware append-site lift for the three pilot areas.  It is not yet the final
builder IR: block/guard predicates are still missing and must be attached in
the next P3 step.

Tool:

```sh
python3 docs/re/tools/lift_event_handler.py "$GAME/TSW.exe" --method floor10
python3 docs/re/tools/lift_event_handler.py "$GAME/TSW.exe" --method roujin
python3 docs/re/tools/lift_event_handler.py "$GAME/TSW.exe" --method openning2
```

Generated TSVs stay outside the repository.

## 1. Floor 10

| Property | Result |
|---|---:|
| Method | `floor10@0x46cbc7` |
| Append rows | 312 |
| Basic blocks with appends | 4 |
| Opcode `0x00` (frame no-op) | 194 |
| Opcode `0x01` (sound/music) | 52 |
| Opcode `0x06` (set tile) | 4 |
| Opcode `0x0a` (start altar sub-event) | 2 |
| Opcode `0x0c` (monster movement) | 54 |
| Opcode `0x0d` (door animation) | 6 |
| Resolved arg1 / arg2 | 312 / 312 |

The counts match the existing inventory: 54 monster moves, 4 map changes and
2 sub-events.  The 312 append sites are now grouped by basic block and carry
literal opcode/argument values where the source is an immediate.

Guard flag: state index `0x6a` (`0x4b8830`), confirmed by the `0x46cbc7`
read.  The writer of that flag is still open.

## 2. `roujin`

| Property | Result |
|---|---:|
| Method | `roujin@0x449708` |
| Append rows | 22 |
| Basic blocks with appends | 4 |
| Opcode `0x00` | 14 |
| Opcode `0x01` | 3 |
| Opcode `0x02` | 2 |
| Opcode `0x08` | 2 |
| Opcode `0x0a` | 1 |
| Resolved arg1 / arg2 | 22 / 22 |

The floor-2 flag is state index `0x62` (`0x4b8810`).  Floor-3/4/6 branches
still need the selector-to-flag map; `0x42..0x45` are `DAT_0048c55c`
selector values, not automatically state flags.

## 3. Opening path

The opening sequence is not one handler.  First-pass results:

| Method | Append rows | Blocks | Notes |
|---|---:|---:|---|
| `openning2@0x464048` | 129 | 5 | 27 `0xb` sub-dispatches, 17 sound, 1 map/tile, 1 actor animation; 120/129 arg2 resolved |
| `openning31@0x4653b8` | 12 | 1 | 4 `0x0a` sub-events, 1 `0xb`, 1 sprite |
| `openning9@0x482e60` | 0 | — | direct effects; no command appends |
| `zenolivework@0x461a0c` | 0 | — | direct effects; no command appends |

The floor-3 `(4,8)` trigger and the one-time launch `open0.bmp` path still
need the exact `mevent`/`ichicheck`/`openning*` entry and predicate.  Do not
use the provisional `3:92:7` open/teleport as the final behaviour.

## 4. What is resolved

* CFG-aware append grouping replaces the address-window `extract_timelines.py`
  inventory for the pilot methods.
* Every pilot append row has a basic-block address.
* Immediate opcode/argument values are resolved (312/312 floor 10, 22/22
  `roujin`).
* The floor-10 and floor-2 flags are corrected.

## 5. What is still missing

* Branch/guard expressions for each block (currently only block addresses).
* Builder-side mutations/calls that occur between appends.
* Exact `roujin` selector/flag mapping for floors 3/4/6.
* Floor-3 opening entry predicate and the one-time launch path.
* Native oracle execution (the harness contract is in
  `14-event-oracle-contract.md`).
* Full builder-IR expression generation and comparison.

## 6. Next P3 steps

1. Attach block entry predicates from the CFG/branch lift to the append rows.
2. Add builder-side `set`/`call`/`map` operations between appends.
3. Recover the `roujin` selector-to-flag map and the opening trigger predicate.
4. Implement the native/synthetic oracle harness from the P2.5 contract.
5. Compare floor 10, `roujin` and opening command buffers/state effects.
