# Event timeline VM (`stackwork`, `0x4526c0`)

Status: **P1 structural recovery complete**.  This document records the
command storage, cursor order, opcode/argument semantics, state effects and
presentation sub-dispatch needed to model the original event VM.  It is based
on the pinned binary:

```
sha256 41cb9844d9bdcf2641a34d4b152d6fbbb0455077abe0e9d7bfed77761a4d2779
```

The reference model and tests are in `docs/re/tools/event_vm.py` and
`docs/re/tools/test_event_vm.py`.

## 1. Storage and cursor

```
DAT_0048c5ac                 command count / current index
DAT_0048c74c + i*6 + 0       opcode (u16)
DAT_0048c74c + i*6 + 2       arg1   (u16)
DAT_0048c74c + i*6 + 4       arg2   (u16)
```

`stackwork` (`0x4526c0`):

1. clears `DAT_0048c5c0`;
2. reads the command at index `DAT_0048c5ac`;
3. dispatches on the u16 opcode;
4. at the common tail (`0x4547e8`) decrements `DAT_0048c5ac` by one.

Handlers append by incrementing the count and writing the three words at the
new index.  The result is a **tail/LIFO timeline**: the last appended command
executes first.  Index 0 is a sentinel; the original normally stops before
executing it.

`timer2timer` (`0x45265c`) calls `stackwork` up to **two times per timer
event**:

```
if count >= 1: stackwork()
if count >= 1: stackwork()
```

The exact timer interval and the mapping to the reimplementation's fixed tick
rate remain a timing task (WS6); the number of commands consumed per timer
event is known.

## 2. No-ops and composites

* Opcode `0x00` is a **one-stackwork-invocation no-op**.  It appends nothing
  and changes no state; it is the usual frame/delay step.
* Opcode `0x12` maps to the same default path as opcode `0x00`: jump-table
  entry `0x452746` points to `0x4547e8`, so it is also a no-op.
* Opcodes `0x09`, `0x0e` and `0x15` read the **previous command** as part of
  a composite:
  * `0x09` uses current `arg1/arg2` plus previous `arg1/arg2` for a
    three-sprite animation;
  * `0x0e` uses current and previous arguments for the battle-hit animation,
    then applies monster damage;
  * `0x15` consumes a preceding `0x15` with the same mode to draw a
    consecutive actor, and `arg2 == 0x79` calls `timerin`.
* `taisen2` (`0x47f7d4`, called by `0xb/10`) reads
  `[base + count*6 + 0xa]`.  Because `0xb/10` decrements the count before
  calling it, this is the original `0xb` command's own `arg2`, not a
  separate companion command.  The whole-binary scanner labels the access
  `cursor_plus_10` to avoid assuming an unadjusted cursor.

## 3. Sub-events are cursor splices, not a call stack

For `0xb` subcommands `1`, `2`, `3`, `5` and `10`:

```
count -= 1
call sub-handler
count += 1
```

The sub-handler appends commands at the decremented index, overwriting the
current `0xb` command.  The normal end-of-step decrement then leaves the last
appended command as the next command to execute.  There is **no separate
event stack**.  Subcommand `6` uses the same pattern around `gameover`.

This resolves the earlier open question about sub-event stacking.

## 4. Opcode reference

Arguments are u16 words from the command.  Signed uses are noted.  “State”
means the int32 array at `0x4b8688`.

| Op | Name | arg1 | arg2 | Effect |
|---:|---|---|---|---|
| `0x00` | frame no-op | — | — | Consumes one stackwork call; no state change |
| `0x01` | sound/media | sound index | — | Dispatch to BGM/WAV/image paths. Cases `7..10` load `\wav\3f.wav`, `\wav\20f.wav`, `\wav\kai.wav`, `\wav\kai2.wav`; cases `1..4` drive media/BGM objects; cases `5..6` call sound play/stop helpers |
| `0x02` | set sprite | sprite id (0 = keep) | — | Sets `DAT_0048c58c` when nonzero; redraws the stat panel |
| `0x03` | altar/stat window | — | — | Draws the player/current tile and calls `zyouout` (`0x449e18`); no direct state mutation |
| `0x04` | player life delta | — | — | `life += defense - monsterAttack*(difficulty+1)`; positive results are forced to 0, then clamped at 0 |
| `0x05` | monster HP delta | — | — | `hp += monsterDefense*(difficulty+1) - playerAttack*mult`; positive results forced to 0, clamped at 0. `mult=2` for Zombie/Zombie Knight/Vampire with Cross, or Dragon with Dragon Slayer |
| `0x06` | set map tile | signed map index | new tile | `map[floor][arg1] = arg2`; redraws the tile |
| `0x07` | add gold | signed amount | — | `gold += arg1 * (LuckyGold + 1)` |
| `0x08` | show item/key UI | — | — | Shows the item/key panels |
| `0x09` | three-sprite animation | sprite 1 | sprite 2 | Third sprite is previous command's `arg1`; fourth value from previous `arg2`; presentation only |
| `0x0A` | start altar sub-event | selector | — | Sets sub-event id `DAT_0048c5a4 = arg1 + 10` and calls `zyouout`; presentation/choice path.  Confirming the choice (`Button1Click`) sets `state[0x50 + id]` for ids `0x0b..0x43`, i.e. `state[0x5a + arg1]` (`17-static-findings.md` §2) |
| `0x0B` | sub-dispatch | subcommand | sub-arg | See section 5 |
| `0x0C` | monster movement | — | — | Calls `monidou` (`0x480384`) |
| `0x0D` | door animation | — | — | Calls `dooropen` (`0x4770e0`) |
| `0x0E` | battle hit + damage | sprite 1 | sprite 2 | Animates using current/previous arguments, then applies `monsterDefense*(difficulty+1) - playerAttack` with multiplier 1, clamped at 0 |
| `0x0F` | stop BGM | — | — | Stops BGM and updates the sound state |
| `0x10` | add life | signed delta | — | `life += arg1`; clamp at 0; if it reaches 0, truncate the timeline and run the game-over path |
| `0x11` | silent tile set | signed map index | new tile | `map[floor][arg1] = arg2` without redraw |
| `0x12` | no-op | — | — | Same default path as `0x00` |
| `0x13` | halve life | — | — | Arithmetic right shift by 1; clamp at 0; if 0, truncate and game over |
| `0x14` | set state field | signed state index | signed value | `state[arg1] = arg2`; direct index into `0x4b8688` |
| `0x15` | draw actor | signed mode | signed map index | `mode=0` draws the player at the map index; `mode=1` draws the map actor/tile. Consumes a preceding same-mode `0x15`; `arg2==0x79` calls `timerin` |
| `0x16` | reset/ending | — | — | Clears option flags `0x4b8904`/`0x4b8908` and calls `gameover` |

Arguments stored in the command are u16; the rows above mark the fields the
original reads through a signed 16-bit load.  `event_vm.s16()` applies that
conversion, so `0xffff` means `-1` for a signed field.  The reference VM's
`timer2_tick()` models the two-call ceiling.

## 5. Sub-dispatch `0x0b`

| arg1 | Action | Cursor effect |
|---:|---|---|
| `1` | `openning31` (`0x4653b8`) | splice |
| `2` | set map index `0x1b` to tile `0x5b`, then `zenolivework` (`0x461a0c`) | splice |
| `3` | `madoushi2` (`0x462560`) | splice |
| `4` | `banpaiawork` (`0x47c978`); `arg2` is the stage, scaled by 5/4 when option A is 1; the call is repeated with `DAT_0048c554` 0 then 1 | direct |
| `5` | `moncheck` (`0x4664e4`) | splice |
| `6` | game-over/ending: option A = `0x2b` on a normal run, option B = 1 on a carried run, then `gameover` | splice |
| `7` | title screen: stop BGM, select `title05.bmp` or `title05R.bmp`, hide message/UI | direct |
| `8` | player/map update `yusyahyouji` (`0x443648`) | direct |
| `9` | player animation `yusyahyouji1` (`0x4433a4`) | direct |
| `10` | `taisen2` (`0x47f7d4`) | splice |
| `0xb` | change floor to signed `arg2`; redraw map and panel; floor 50 has a special spawn/position path | direct |
| `0xc` | `openning9` (`0x482e60`) | direct |
| `0xd` | hide message/UI | direct |
| `0xe` | hide all event images | direct |
| `0xf` | screen fade/transition | direct |

## 6. State indices for opcode `0x14`

The state base is `0x4b8688`; `0x14` writes `state[arg1] = arg2` with no
remapping.  The public index map is the one in `src/state.h` and
`03-state-and-save.md`:

| Index | Field |
|---:|---|
| 0 | life |
| 1 | attack |
| 2 | defense |
| 3 | gold |
| 4 | current floor |
| 5 | highest floor |
| 6 | player x |
| 7 | player y |
| 8/9/10 | yellow/red/blue keys |
| 15/16 | weapon/shield display level |
| 17..31 | special-item slots 0..14 |
| 32..36 | sword-picked flags |
| 37..41 | shield-picked flags |
| 42.. | event/treasure/monster flags |
| 88 | facing |
| 159/160 | option flags A/B |
| 164/165 | checksums |
| 170 | save version |

This resolves the earlier open item: the VM state index is the direct
`GameState` int index, not a separate private table.

## 7. Battle-side formulas used by the VM

```
difficulty = state[ST_OPTION_A]

opcode 0x04:
    delta = state[ST_DEFENSE] - monster.attack * (difficulty + 1)
    if delta > 0: delta = 0
    state[ST_LIFE] += delta

opcode 0x05:
    mult = 2 if Cross and monster in {12,13,17}
         or Dragon Slayer and monster == 19
         else 1
    delta = monster.defense * (difficulty + 1) - state[ST_ATTACK] * mult
    if delta > 0: delta = 0
    monster_hp += delta

opcode 0x0e:
    delta = monster.defense * (difficulty + 1) - state[ST_ATTACK]
    if delta > 0: delta = 0
    monster_hp += delta
```

All results clamp at 0.  These match the normal battle formulas in
`06-data-tables.md`; the event VM uses the same fields.

## 8. Evidence map

| Item | Address |
|---|---|
| dispatch and default tail | `0x4526c0`, `0x4526f6`, `0x4547e8` |
| opcode jump table | `0x452706` |
| sound/media case | `0x452762` |
| `0xb` sub-dispatch | `0x4531da` |
| opcode `0x14` | `0x45431f` |
| opcode `0x15` | `0x45433b` |
| opcode `0x16` | `0x4547d3` |
| `timer2timer` two-call loop | `0x45265c` |
| command buffer writers | `docs/re/tools/scan_event_buffers.py` output |

### P0 inventory result

For the pinned binary the scanner reports:

* 2,862 command-opcode writes (append sites), 42 base-address loads, and
  **0 unresolved candidates**;
* the known floor-handler counts are reproduced: floor 10 = 312,
  floor 40 = 253, floor 24 = 236, floor 20 = 77, floor 32 = 78,
  floor 42 = 58 (the older note said 57);
* the old extractor window omitted `handan` (362), `moncheck` (642),
  `zenolivework` (36), and several NPC/room handlers; these are now included;
* 2,204 accesses are marked `+fallback` and `confidence=medium` because they
  depend on case blocks seeded from a method entry after the recovered CFG
  could not connect them.  They are inventory candidates for WS3, not final
  executable timelines.

The generated TSV stays outside the repository.

## 9. Reference model and limits

`docs/re/tools/event_vm.py` models the state effects above and records
presentation calls instead of rendering VCL images/audio.  It supports
injected sub-event handlers so the splice behaviour can be tested.

```sh
python3 docs/re/tools/test_event_vm.py
```

Current limits:

* opcode `0x01` media/BGM internals are logged by index, not emulated;
* opcodes `0x03` and `0x0A` altar-window content is modelled as a
  presentation call; the detailed window/choice logic belongs to `zyouout`;
* presentation effects of `0x09`, `0x0e` and `0x15` are logged, not rendered;
* sub-handlers such as `openning31`, `zenolivework`, `madoushi2`,
  `banpaiawork`, `moncheck` and `taisen2` are injected or logged; their
  builder timelines are WS3 work.

## 10. Remaining timing questions

* The exact original `timer2` interval and the relation between its calls and
  real animation frames still need to be recovered.  First source: the
  `Interval` properties of the timer components in the form data; second: any
  runtime setter.  The engine's event tick takes its period from this value
  (`../design/01-event-system.md` §7).
* Do not assume the reimplementation's 60 Hz contract is the original rate.
  The VM's command-consumption rule is known (`timer2timer` executes at most
  two commands per timer event); the wall-clock mapping is not yet.
