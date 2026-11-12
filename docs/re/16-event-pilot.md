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
python3 docs/re/tools/lift_event_handler.py "$GAME/TSW.exe" --method floor10 --dir /tmp/floor10-ir
```

`--dir` writes:

* `appends.tsv` — append rows with opcode/args, block address and block entry
  edge text;
* `blocks.tsv` — block start/end, last instruction, predecessor/successor
  edges;
* `ops.tsv` — builder-side calls, guards, global writes and byte-memory
  writes.

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

The floor-2 flag is state index `0x62` (`0x4b8810`).  The floor-16 old-man
gate is state index `0x6d` (`0x4b883c`, selector `0x1b` when x=9).  Floor-3/4/6
still need their selector/flag map; `0x42..0x45` are `DAT_0048c55c` selector
values, not automatically state flags.

## 3. Opening path

The opening sequence is not one handler.  First-pass results:

| Method | Append rows | Blocks | Notes |
|---|---:|---:|---|
| `openning2@0x464048` | 129 | 5 | 27 `0xb` sub-dispatches, 17 sound, 1 map/tile, 1 actor animation; 120/129 arg2 resolved |
| `openning31@0x4653b8` | 12 | 1 | 4 `0x0a` sub-events, 1 `0xb`, 1 sprite |
| `openning9@0x482e60` | 0 | — | direct effects; no command appends |
| `zenolivework@0x461a0c` | 36 | 1 | 17 three-sprite animations, 18 waits; 36/36 args, 19/36 arg1 resolved |

The floor-3 `(4,8)` trigger is now resolved as a **map-tile action**, not an
`ichicheck` or `openning2` event:

```
floor == 3
raw 0x07 -> handan 0x4447d2 -> mevent 0x46397c
          -> mevent floor-3 case 0x463a2c
             clear current tile to 0x06
             append opcode 0x0a, arg1=0x0a, dispatched selector 0x14 (20)
             append waits
             DAT_0048c558 = 0x46
             call zenolivework (0x461a0c)
             -> 36 appends (17 three-sprite animations, 18 waits)
```

The one-shot mechanism is the tile mutation `0x07 -> 0x06`; no separate guard
flag is needed for the floor-3 map action.  The map action itself keeps the
player in place; any later prison teleport belongs to the sub-event
presentation.  The `0x0a` selector `0x14` enters `zyouout` for the
dialogue/altar side of the sequence.

`openning2` is **not** the floor-3 trigger: its call site `0x44e04b` is inside
the new-game button path (`Button1Click`, method table `0x44da10`).  It is the
separate launch/opening sequence that also uses `openning31`; the one-time
`open0.bmp` launch remains a distinct path.

## 4. Builder edges and operations

`--dir` now emits block and operation rows:

| Method | Blocks | Ops |
|---|---:|---:|
| floor 10 | 6 | 10 |
| `roujin` | 43 | 28 |
| `openning2` | 31 | 16 |
| `openning31` | 22 | 68 |

The append rows now carry a `block_entry` text showing up to four predecessor
edges, e.g.:

```
0x46cbd4:fallthrough:not(DWORD PTR ds:0x4b87f0 == 0x0)
```

Simple `cmp`/`test` forms are converted to expressions; otherwise the raw
branch instruction is kept.  This is an **edge witness**, not a complete
symbolic predicate over map/equipment state.  The `ops.tsv` rows classify
builder-side `call`, `guard`, `write_global` and `write_mem` operations in the
same blocks.

### Builder IR and synthetic trace

`build_event_ir.py` merges the three TSVs into `builder_ir.json`.
`run_synthetic_oracle.py` walks the builder CFG from an `--entry-block`,
applies path-filtered `write_global` effects (including `0x48c...` aux
selectors), executes the command list through `event_vm`, and records
state/map diffs, builder writes and unresolved edges.

First traces:

| Method | Commands | Result |
|---|---:|---|
| floor 10 | 276 / 312 lifted | complete trace for the armed snapshot; 3 map tile diffs; classified external exit |
| `roujin` (floor-2 entry `0x4497e4`) | 6 / 22 lifted | flag `0x62=1`, selectors `0x19`/`0x42`; stops at dynamic stat writes/unknown selector edge |
| `zenolivework` | 19 / 36 lifted | 17 unresolved arg1 values skipped; 2 unresolved edges |
| `openning2` | 2 / 129 lifted | stops at an unknown branch; incomplete |
| `openning31` | 0 / 12 lifted | path stops at dynamic writes; incomplete |

This is a synthetic oracle, not native-code execution.  `complete` is false
whenever a path has an unknown edge, missing internal block, unresolved
argument or unsupported builder effect.

### Static timeline composer

`compose_event_timeline.py` is the trace-free P3 composer.  It walks the
builder CFG, applies static path/loop/effect contracts, and inlines known builder
callees at their call sites.  The native oracle is used only to compare the
result afterwards; it is not an input.

Exact static command streams, with zero unresolved builder effects:

| Case | Static commands | Native commands |
|---|---:|---:|
| floor 10 | 462 | 462 |
| `roujin` floor 2 | 6 | 6 |
| `roujin` floors 3/4/6 | 0 | 0 |
| `zenolivework` | 36 | 36 |
| new-game `openning2` | 651 | 651 |
| floor-3 `handan` trigger | 41 | 41 |

Key static contracts used by the composer:

* `doorstackin` consumes the preceding door command and derives its door
  arguments from it; the consumed command is replaced, not duplicated.
* `roujin` uses the floor byte/pointer switch at `0x449767`/`0x449798`,
  selector/state path contracts, and the 3% stat boost at `0x449805`.
* `openning2` uses a 41-iteration loop at `0x464141` with computed `0x0b`
  sprite arguments, deferred argument writes, and facing selectors.
* `madoushilivework` is a 42-command callee; the static openning2 stream
  includes it.
* `handan` uses the tile switch at `0x4444e3`, the `mevent` floor switch at
  `0x4639e8`, the one-shot tile write, and `zenolivework`.

The composer emits initial/final state, initial/final map bytes, state/map
deltas, effective auxiliaries and the pinned binary digest.  The integration
test runs the composer and native oracle on identical synthetic snapshots and
compares every command word plus final state/map and modeled auxiliaries.  The
only deliberately skipped auxiliary is the handan final `event_selector`, which
is presentation-path state rather than a builder command effect.

### Pilot path witnesses

Same-input static/native parity is exercised for these witnesses:

| Pilot | Covered witnesses | Not covered yet |
|---|---|---|
| floor 10 | armed flag path; guard-disabled flag path | other branch-selected floor-10 variants |
| `roujin` | floor 2 flag clear/set; floors 3/4/6 selector paths | other selector/gate combinations |
| `zenolivework` | full 36-command animation path | none found beyond this path |
| `openning2` | width-32/facing-4 fire path | other form widths/facing branches |
| floor-3 `handan` | raw-`0x07` trigger; cleared `0x06` tile | other raw tiles/NPC families |

The static/native differential test compares every command word, final
state/map, and modeled auxiliaries on these witnesses.

### Native pilot traces

`run_native_oracle.py` executes the original handlers with typed presentation
stubs (`stub_mode="presentation"`), pinned hash checking and fail-closed
unknown-call handling.  All four main pilots now return normally and are
untainted:

| Case | Entry | Native commands | Stubs | Result |
|---|---|---:|---:|---|
| floor 10 | `0x46cb1c` | 462 | 19 | `returned`, untainted |
| `roujin` floor 2 | `0x449708` | 6 | 50 | `returned`, untainted |
| new-game `openning2` | `0x464048` | 651 | 49 | `returned`, untainted |
| `zenolivework` | `0x461a0c` | 36 | 28 | `returned`, untainted |
| floor-3 `handan` trigger | `0x444490` | 41 | 164 | `returned`, untainted |

Native counts include commands appended by dispatched subhandlers such as
`doorstackin`, so they are larger than the main-handler lift rows.  The
`roujin` native prefix is exactly the synthetic prefix
`(0,0,0),(2,0,0),(0,0,0),(1,2,0),(0,0,0),(0,0,0)`; `zenolivework` also reaches
the full 36 lifted commands.  The traces hash the effective state/map/aux
inputs, embed the effective input manifest plus initial/final state and map
bytes, record auxiliary deltas (for example `roujin` message/event selectors
25/66) and list normalized helper invocations.  Virtual presentation returns
are limited to vtable slots `0x0c/0x1c/0x20/0x28/0x2c/0x80`; unknown slots and
unclassified transfers fail closed.  `openning31` remains native-incomplete
because it reaches an unclassified Delphi string/allocator path (`0x403924`).
The floor-3 `handan` trigger now runs natively from `(4,8)`/index 92: it
clears the tile `0x07→0x06`, leaves player x/y unchanged, starts with
`(0x0a,0x0a,0)`, and completes the `zenolivework` subevent sequence (41
commands).  The static `0x14` subevent selector still needs a dedicated aux
trace; the native `0x48c55c` value at return is `0x67`.

## 5. What is resolved

* CFG-aware append grouping replaces the address-window `extract_timelines.py`
  inventory for the pilot methods.
* Every pilot append row has a basic-block address and predecessor-edge text.
* Simple `cmp`/`test` branch predicates are attached as edge expressions;
  complex conditions remain heuristic.
* Builder-side calls, guards and global/byte writes are inventoried per block.
* `builder_ir.json` and determinism-checkable synthetic oracle traces are
  produced for the pilot methods.
* The floor-3 raw-`0x07` → `mevent` → `zenolivework` path is statically traced
  and statically composed to the exact 41-command stream.
* Static composition reproduces floor 10 (462), `roujin` floor 2 (6),
  `zenolivework` (36), `openning2` (651), and the floor-3 trigger (41)
  command-for-command with zero unresolved builder effects.
* `roujin` floors 3/4/6 selectors are recovered: floor 3 `0x1a/0x43`, floor 4
  `0/0x44`, floor 6 `1/0x45`; under the default snapshot they append no
  commands.
* The floor-10 and floor-2 flags are corrected.

## 6. What is still missing

* The one-time `open0.bmp` launch path (separate from the floor-3 trigger).
* `openning31` closure at the Delphi string/allocator path (`0x403924`).
* The static `0x14` subevent-selector trace; the floor-3 native case currently
  ends with `0x48c55c=0x67`.
* General symbolic loop bounds and predicates beyond the pilot contracts;
  the composer currently uses explicit pilot loop/path contracts.
* Bulk lifting of the remaining `ichicheck`/NPC handlers (P4).

## 7. P3 pilot acceptance and follow-up

P3 pilot acceptance work:

1. Keep the path-witness inventory above current as pilot branches are added.
2. Record any newly discovered uncovered pilot path explicitly in this table.

Separate new-game/launch follow-up:

1. Recover the one-time `open0.bmp` launch boundary.
2. Close `openning31` with a typed string/allocator model.

P4 generalization work:

1. Replace the pilot-specific loop/path contracts with reusable symbolic forms
   before bulk lifting.
2. Expand predicates and builder effects to the remaining handlers.
