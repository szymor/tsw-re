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

The timer interval is recovered statically; see section 10.

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
| `0x03` | death window | — | — | Draws the player's cell, Timer2 off, BGM `state[0x5a]` → `0x0e` when on, `state[0xc]` = 1, `g_subevent_id` 9, `g_dialog_selector` `0x1e`, then `zyouout`, which appends the game-over timeline and calls `timer2on` (`16-event-pilot.md` §11) |
| `0x04` | player life delta | — | — | `life += defense - monsterAttack*(difficulty+1)`; positive results are forced to 0, then clamped at 0 |
| `0x05` | monster HP delta | — | — | `hp += monsterDefense*(difficulty+1) - playerAttack*mult`; positive results forced to 0, clamped at 0. `mult=2` for Zombie/Zombie Knight/Vampire with Cross, or Dragon with Dragon Slayer |
| `0x06` | set map tile | signed map index | new tile | `map[floor][arg1] = arg2`; redraws the tile |
| `0x07` | add gold | signed amount | — | `gold += arg1 * (LuckyGold + 1)` |
| `0x08` | show item/key UI | — | — | Shows the item/key panels |
| `0x09` | masked tile composite (`kasanegaki` `0x44439c`) | map index | mask tile | Previous command gives background tile (`arg1`) and sprite tile (`arg2`). Pixel result is `R = (B | ~M) & (S | M)` per channel; all three are 1-based Chtable tiles. Masks `0xa6`..`0xb6` run white→black and produce the fade-in/out of Zeno and the guards; presentation only |
| `0x0A` | start altar sub-event | selector | — | Sets sub-event id `DAT_0048c5a4 = arg1 + 10` and calls `zyouout`; presentation/choice path.  Confirming the choice (`Button1Click`) sets `state[0x50 + id]` for ids `0x0b..0x43`, i.e. `state[0x5a + arg1]` (`17-static-findings.md` §2) |
| `0x0B` | sub-dispatch | subcommand | sub-arg | See section 5 |
| `0x0C` | monster movement | from map index | to map index | `monidou` (`0x480384`): `map[floor][arg2] = map[floor][arg1]` (`0x480a70`), then `map[floor][arg1] = 6` (`0x480a83`).  Animation: a 32-iteration loop slides the masked sprite (Chtable/Chtableb) one pixel per iteration inside the same tick, but the on-screen copy only refreshes every 8 px, so it shows as 4 steps of 8 px; the loop blocks the next tick while it draws |
| `0x0D` | door animation | signed map index | door kind | Calls `dooropen` (`0x4770e0`).  For kinds `1..12` it sets the Timer2 interval to 8/10/12 ms (by speed), takes the animation progress `DAT_0048c558` from the **previous** command's `arg2` (`0x10` calls `timerin`; scaled by 5/4 in the 800×500 layout), draws the frame and **writes `map[floor][arg1]`**: `4` for kind 5, `8` for kind 8, `6` otherwise (every frame, not only the last).  Kind 0 or > 12 does nothing.  Frames: the base tile (floor `6`, or tile `13` for kinds 9..12) with rectangles of the door tile (kind 1..3 → tile 1..3, 4/5 → 4, 6 → 5 masked, 7/8 → 8; kinds 9..12 copy floor) copied at offsets of `p` or `2p` px: halves sliding apart (1..3), quadrants (4 out, 5 in), slide down (7), rise (8), wipes (9..12); see `event_engine_door_frame` |
| `0x0E` | battle hit + damage | map index | mask tile | Draws the op-`0x09` composite inline (`0x453a36`, previous command = background/sprite), then applies `monsterDefense*(difficulty+1) - playerAttack` with multiplier 1, clamped at 0 |
| `0x0F` | stop BGM | — | — | Stops BGM and updates the sound state |
| `0x10` | add life | signed delta | — | `life += arg1`; clamp at 0; if it reaches 0, truncate the timeline and run the game-over path |
| `0x11` | silent tile set | signed map index | new tile | `map[floor][arg1] = arg2` without redraw |
| `0x12` | no-op | — | — | Same default path as `0x00` |
| `0x13` | halve life | — | — | Arithmetic right shift by 1; clamp at 0; if 0, truncate and game over |
| `0x14` | set state field | signed state index | signed value | `state[arg1] = arg2`; direct index into `0x4b8688` |
| `0x15` | draw actor | signed mode | signed map index | `mode=0` draws the player at the map index; `mode=1` draws the map actor/tile. Consumes a preceding same-mode `0x15`; `arg2==0x79` calls `timerin` |
| `0x16` | try again | — | — | Clears option flags `0x4b8904`/`0x4b8908` and calls `gameover` (`syokidata` + initial map, `syokidata2` sets `g_tl_count` = 0, so the tail leaves -1) |

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
| `4` | `banpaiawork` (`0x47c978`), presentation only: eight bats (`0x53`) converge on the 3×3 block at cells (4..6, 4..6) while the vampire (`0x5f`) fades in through mask `0xa6 + stage/2`.  `arg2` is the stage in px, scaled by 5/4 only in the 800×500 layout (`option.dat` byte 8, `0x489ba4`); the call is repeated with `DAT_0048c554` 0 then 1 for the two map buffers | direct |
| `5` | `moncheck` (`0x4664e4`) | splice |
| `6` | game-over/ending: option A = `0x2b` on a normal run, option B = 1 on a carried run, then `gameover` | splice |
| `7` | title screen: stop BGM, select `title05.bmp` or `title05R.bmp`, hide message/UI | direct |
| `8` | player/map update `yusyahyouji` (`0x443648`) | direct |
| `9` | player animation `yusyahyouji1` (`0x4433a4`) | direct |
| `10` | `taisen2` (`0x47f7d4`) | splice |
| `0xb` | change floor to signed `arg2`; redraw map and panel; floor 50 has a special spawn/position path | direct |
| `0xc` | `openning9` (`0x482e60`): phase = **previous** command's `arg1`, frame = `arg2`; sets the Timer2 interval per phase (section 12), phase 8 stops Timer2 | direct |
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
* opcode `0x0A`'s window is the engine's (`zyouout` in `event_engine.c`);
  opcode `0x03` appends the game-over timeline in the VM
  (`event_vm_death_timeline`) and the engine turns Timer2 on;
* the VM logs the presentation of `0x09`, `0x0e` and `0x15`; the engine draws `0x09` and `0x0e` (the same `kasanegaki` composite) as actor tiles and `0x15` as the floor wipe;
* sub-handlers such as `openning31`, `zenolivework`, `madoushi2`,
  `banpaiawork`, `moncheck` and `taisen2` are injected or logged; their
  builder timelines are WS3 work.

## 10. Timer2 interval (static)

Source: the `TTSW10` form data and every call of `TTimer.SetInterval`
(`0x42c464`, the `Interval` property setter in the `TTimer` RTTI).  Field
offsets come from the `TTSW10` published-field table: `Timer1` `+0x1b4`,
**`Timer2` `+0x2ec`** (`OnTimer = timer2timer`), `Timer3` `+0x30c`
(`timer3ontimer`), `Timer4` `+0x41c` (`timer4ontimer`).  The speed check
items are `High1` `+0x3a8`, `Middle1` `+0x3ac`, `Low1` `+0x3b0`.

| Setter | Timer2 interval | Notes |
|---|---|---|
| form data (`TTSW10`) | 300 ms, `Enabled = False` | design-time default |
| `speedhigh` `0x47f47c` | 150 ms | also Timer1/Timer3; `option.dat` byte 3 = 1 |
| `speedmiddle` `0x47f4e0` | 250 ms | default (`NormalOption1Click` byte 3 = 2) |
| `speedlow` `0x47f544` | 350 ms | byte 3 = 3 |
| `dooropen` `0x4770e0` (op `0x0d`, kinds 1..12) | 8 / 10 / 12 ms | high / middle / low |
| `kaidanwork` `0x442dc8` (stairs) | 2 / 6 / 10 ms | then `timer2on` |
| `openning9` `0x482e60` | phase-specific: 150/250/350, 30/50/70, 130/200/300/100, 60, 450 ms | opening sequence; restores the speed value at `0x48322b` |

All 57 direct `call 0x42c464` sites were enumerated; the remaining ones set
Timer1/Timer3 (speed) or Timer4 (the `open0` intro, 350/250/150 ms).
`timerin` (`0x47f5a8`) calls whichever of `speedhigh`/`speedmiddle`/
`speedlow` is checked, i.e. restores the speed interval.  `dooropen` calls it
on the last door frame (progress `0x10`) and op `0x15` calls it for
`arg2 == 0x79`, so a door's 8..12 ms lasts only for its own animation.
`kaidanwork` does not call it: after a staircase the interval stays fast
until the next setter.  `timer2on` (`0x44bc0c`) only stops Timer3,
calls `itemdel` and enables Timer2; `timer2timer` disables Timer2 and calls
`itemlive` once the count drains (unless `g_stackwork_flag`).

Practical note: Windows `SetTimer` clamps intervals below 10 ms
(`USER_TIMER_MINIMUM`) and delivers `WM_TIMER` at the system tick
(typically ~15.6 ms), so the 2..12 ms values run at roughly 10–16 ms on real
systems.  This is a property of the host OS, not of the binary.

Engine contract: `src/event_vm.h` exposes the three tables
(`event_vm_speed_interval_ms`, `event_vm_door_interval_ms`,
`event_vm_stairs_interval_ms`); the VM tracks `interval_ms`, starting at the
speed value, switching to the door value when op `0x0d` runs and back on
`timerin`.

## 11. Dialogue suspension (P3b)

Recovered with the execution oracle (`tools/run_execution_oracle.py`,
`16-event-pilot.md` §8):

* opcode `0x0a` sets `g_subevent_id` (`0x48c5a4`) to `arg1 + 10` and
  `0x48c5c8` to `arg1`, **disables Timer2** (`0x45318c`: `xor edx,edx;
  mov eax,[ebx+0x2ec]; call TTimer.SetEnabled` at `0x453194`) and opens the
  `zyouout` window.  Commands left in the buffer are not consumed;
* confirming the window runs `Button1Click` (`0x44da10`): for
  `10 < id < 0x44` it stores `state[0x50 + id] = 1` (`0x44da2a`), then
  dispatches ids `0..0x1a` through the table at `0x44da4f` and higher ids
  through `0x44dad3` (for example case 20 at `0x44e022` calls `openning2`).
  A case may append a new timeline over the remaining one; the handler ends
  in `timer2on` (`0x44bc0c`), which re-enables Timer2;
* `timer2timer` makes two `stackwork` calls per timer event even when the
  first one disabled Timer2, so a command right after `0x0a` in the same
  event still runs;
* the engine models this: Timer2 is explicit engine state; op `0x0a` stops
  it and opens the window with `ListBox2` text (`18-messages.md`); Enter runs
  the `button1click` builder (a port of `Button1Click`), whose cases may
  append commands and re-enable Timer2.  Confirm cases that are not ported
  yet are logged and counted in `EventEngine.unported`; nothing falls
  back to a script runtime any more.

Choice suspension and event-driven battles (`taisen2`) are ported; the
remaining open point is the wall-clock relation between `timer2timer` calls
and presentation.

## 12. `openning9` phases (opening and endings)

`openning9` reads the phase from the previous command's `arg1`
(`0x48c748 + count·6`) and the frame from its own `arg2`.  It draws the
opening/ending pictures and owns their pacing (`option.dat` speed
high/middle/low):

| Phase | Frames | Picture | Timer2 interval |
|---|---|---|---|
| 0 | `0xe8..0xf7` | `open1.bmp` | at `0xf7`: back to 150/250/350 ms |
| 2 | … `0xf8` | `open0.bmp` | at `0xf8`: back to 150/250/350 ms |
| 1 | 0, 100..349, 400 | `open2.bmp` revealed over `open1.bmp` row by row | frame 0: 30/50/70 ms; frame 400 (500 with 40-px tiles): panels restored, 130/200/300 ms |
| 4 | — | ending (`End20`/`End26`) | 100 ms |
| 5 | — | ending | 60 ms |
| 6 | 1..0x11 | `open0`, `End05`..`End20` | 450 ms |
| 7 | — | "Thank you for playing!" + ending MIDI | — |
| 8 | — | ending buttons | Timer2 **off** |

`event_vm_opening9_interval_ms()` / `opening9_interval_ms()` implement the
interval rule; the engine draws phases 0–2 (`EventCutscene`).
