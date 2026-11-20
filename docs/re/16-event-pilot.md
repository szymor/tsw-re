# Event method, execution oracle and ported handlers

How every event was recovered from the original, the execution oracle that
checks the remake against it, and notes per ported handler.  Sections are
numbered as the code and the other notes cite them; §1–7 recorded the
first three pilot handlers (floor 10, `roujin`, the opening) and are in
the Git history.

## Method

The events were recovered **without recorded gameplay**, from the pinned
binary (`sha256 41cb9844...2779`, `14-event-oracle-contract.md`) alone:

* **Structure statically.**  The switch tables, call graph, append sites
  and guard predicates come from the disassembly (`13-event-triggers.md`,
  `scan_event_*.py`, `lift_event_handler.py`); this static map says which
  blocks, edges and append sites exist.
* **Behaviour by running the original.**  The native oracle
  (`native_event_oracle.py`, contract in `14-event-oracle-contract.md`)
  runs a handler's own x86 code on a synthetic snapshot and returns its
  command words and state/map/aux effects; the execution oracle (§8) then
  runs the timeline it built.  What it returns counts as evidence, like a
  lifted instruction.  Witnesses are chosen until the static map is covered
  (§9).
* **Fail closed.**  An unknown call, an unmodelled global or a truncated
  capture makes a run fail, never "probably right".
* **Two levels.**  A handler is a *builder program* (guards, mutations,
  calls, appends); the *command timeline* it produces is separate.  The
  remake ports the builder to Lua (`12-event-vm.md` §13) and is accepted on
  the golden vectors (`export_event_vectors.py`, committed in
  `tests/vectors`).
* The static composer (`compose_event_timeline.py`) reproduced the three
  pilots command for command and was then frozen; it stays as a regression
  check and is not extended.

## 8. P3b execution oracle

`run_execution_oracle.py` runs the builder through the native harness, then
keeps calling the original `stackwork` (`0x4526c0`) one command at a time,
with write hooks on the state and map blocks.  Opcode `0x0a` disables Timer2
(`0x453194`: `TTimer.SetEnabled(form+0x2ec, False)`) and opens the `zyouout`
window, so the drain stops there; `--complete-dialogs` then models the
player's confirmation by calling the original `Button1Click` (`0x44da10`),
which dispatches on `g_subevent_id` (`0x48c5a4`, table `0x44da4f`), may
append a new timeline, and re-enables Timer2 through `timer2on`
(`0x44bc0c`).  `--permissive-vcl` returns 0 from unclassified Delphi RTL/VCL
calls in the drain and marks the run tainted; game code still runs
natively and all writes are hooked, so the state/map effects below are
attributed to original instructions, while presentation parity is not
claimed.

```sh
python3 docs/re/tools/run_execution_oracle.py --case handan_floor3 \
    --permissive-vcl --complete-dialogs --max-steps 4000 --summary
```

| Witness | Drain | Result |
|---|---|---|
| `roujin` | 6 steps, drained | no drain-side writes; builder writes `0x62`, attack/defense +3%, `0x42` (`0x449cc0`) |
| `zenolivework` | 36 steps, drained | no writes |
| `floor10` | 462 steps, sub-event 23 confirmed | first command executed is the tail `0x0a/13`; drain writes only map tiles (`monidou` `0x480a70/0x480a83`, op `0x06`); `0x67` set by `Button1Click`; no `0x6a` |
| `handan_floor3` | 701 steps, 6 dialogue rounds | full opening, below |
| `roujin_floor3` | stopped at op `0x08` | result of the first permissive run; not re-checked in this table since the typed VCL contracts below were added |

**Floor-3 opening, end to end** (pinned binary, no gameplay trace):

1. builder: `idou` moves the player onto `(4,8)`, `mevent` clears the tile
   `0x07 → 0x06` (`0x463a54`) and queues 41 commands;
2. the Zeno animation runs; the tail `0x0a/10` opens sub-event 20 (Zeno's
   speech) and suspends the timeline;
3. confirming sub-event 20: `state[0x64] = 1`, `Button1Click` case 20 calls
   `openning2`, which appends 651 commands; the title (`0x0b/7`), image
   (`0x0b/14`) and 216 `openning9` frames (`0x0b/12`) follow;
4. sub-event 21 (`0x0a/11` inside `openning2`) suspends again; confirming
   it sets `0x65`;
5. `0x0b/1` → `openning31` performs the capture (`0x4655e0..0x465647`):
   floor 2, floor limit 2, `(x,y) = (2,7)`, life 400, attack 10, defense 10,
   `state[0xc] = 1`, weapon/shield display levels 0, `state[36] = 0`,
   `state[41] = 0`, and queues sub-events 11, 11, 12, 13;
6. confirming them sets `0x5b`, `0x5c`, `0x5d`; sub-event 13 clears
   `state[0xc]` (`0x44dc73`) and the timeline is idle.

The capture writes no floor-2 map tile: the prisoner marker `0x15` at
floor-2 index 68 is already in the initial map.  The final state matches the
values previously captured live (400/10/10, prison `(2,7)`).

**Fairy (`yousei`, probe).**  On a floor-10 snapshot `yousei` appends 9
commands including `0x0a/16`; confirming sub-event 26 writes
`state[0x6a] = 1` at `0x44da2a`.  This is the floor-10 area-1 flag setter.

The drain mirrors `timer2timer`: when `0x0a` is the first `stackwork` call
of a timer event, the second call still runs before the suspension.  Native
contracts added for this work (`native_event_oracle.py`): `mhyouji`
(`0x442c38`) runs natively (it clears `state[0xa6]`, so it is not
presentation-only); `TBitmap.GetCanvas` (`0x41dad8`) returns a writable
scratch object; `0x417e44` and `TCanvas.CopyRect` (`0x41a530`) pop their
stack arguments; vtable slot `0x38` (`TStrings.Clear`) is a presentation
return.  With them the native `Button1Click` runs for every ported case and
`button1click_*` golden vectors exist.

**Strict drains and drain parity.**  With the typed contracts of
`14-event-oracle-contract.md` §4 every pilot drain runs without
`--permissive-vcl` (untainted), with identical results.  The oracle confirms
every window `zyouout` opened (a code hook at `0x449e18`), so NPC dialogues
(sub-event 0, e.g. `roujin`'s Orb of the Hero 2 → 1) are confirmed too.
`export_event_vectors.py` stores each complete drain in the vector
(`drain_dialog`, `xstate`, `xmap`), and `test_event_builders` replays the
builder through the real engine (Timer2 ticks, confirms) and compares the
confirmed sub-event sequence and the final state and map: **32 of 32 drained
witnesses matched** at the time, including the whole opening.  This exposed one VM gap,
now fixed: opcode `0x0c` (`monidou`) moves the map tile.

**Status-bar parity.**  The oracle runs the Delphi string helpers for real
(`GetMem`, `LStrCat`/`LStrCat3`/`LStrCatN`, `IntToStr`) and gives
`ListBox2.Items.Get` an object of its own that returns `"{index}"`, so the
status string a handler builds (`0x4b8684`) comes out as tokens and numbers:
taisen's `"{199} {96}. {200} 16{201}"`.  Every vector records the text at
each `disp` and the string at the end (`display`, `status`); the C test
runs the builder through the engine with a `"{i}"` message table and the
original's code literals (`ui.tsv` beside the vectors) and compares.  The
one accepted difference is the remake's reworded victory line (the same
first entry, monster and gold).  Re-exporting with the string runtime left
every other line of the 468 vectors unchanged.

**The opening is ported** (`src/events/builders.lua`): `openning2`
(651 commands, exact), `madoushilivework`, `openning31` (normal and
second-run variants), and `Button1Click` with all its cases except the
`0x1a` difficulty-0 branch and id 9.

**The thief / floor-2 prisoner is ported** (`touzoku`, `0x465900`, with
`doorstackin`, `0x4660d0`, and `soundcheck`, `0x47c8f8`).  15 witnesses
(`touzoku_*`, `handan_touzoku_*`) cover floor 2 (the cell from x 2..3,
the tunnel from x 0, the later meeting from x 9..10 with and without
`state[0x60]`), floors 15, 29 (three branches), 35 (two) and 50, and a floor
without a thief.  All match builder and drain parity (47 of 47 drained
witnesses now).  The cell path appends 39 commands: sub-event 14, sprite 1,
the cell door at index `0x43` (kind 7, 16 door frames from `doorstackin`)
and sound 1; confirming (case `0x0e`) walks him 68 → 67 → 66 → 77.  The
tunnel path is sub-event 15; case `0x0f` walks him out via 88 and 99.
With sound on, `soundcheck` writes the floor's BGM id to `state[0x5a]`
(floor 2: 5), a real state effect.  New typed contracts:
`TMediaPlayer.SetDeviceType` (`0x431c8c`), `TMediaPlayer.SetNotify`
(`0x431c5c`) and `TControl.GetText` (`0x4135a4`, reached by `zyouout` for
arg1 5).

**Secret walls** (`handan` case 9, `0x4447df`): two witnesses
(`handan_secret_wall_floor5`, `_floor9` with sound on), 37 commands each
(the wall is door kind 7 through `doorstackin`), builder and drain parity
(49 of 49).

**Merchants and choice suspension.**  `syounin` (`0x449014`) shows its
offer in RichEdit1 with Button2 (Yes) and Button7 (No) instead of calling
`zyouout`, and later talks show Button1 directly.  The execution oracle
now gives Button1 and Button2 their own dummy objects and watches
`TControl.SetVisible` (`0x413500`): Button1 shown counts as a dialogue to
confirm (like `zyouout`), Button2 shown stops the drain as
`choice_pending` (the player has to answer).  The answers are separate
witnesses on `Button2Click` (`0x44e1cc`) and `Button7Click` (`0x44ea90`).
23 witnesses (offers on floors 2/6/7/12/28/47, a floor without a merchant,
thank-you and talked-out states, buying with enough and too little gold,
the floor-28 key buyer with and without keys, life and super-mattock
rewards, No) match builder and drain parity: 72 of 72 overall.  New aux
globals: `shop_price`, `shop_flag`, `shop_talk_flag`, `choice_mode`
(a word, so the harness writes it with width 2) and `battle_turns`.

**Floor 10 and the fairy.**  `ichicheck` (`0x46cb1c`) floor 10
(`0x46cbc7`) is straight-line code: the builder transcribes it (doors via
`doorstackin`, a list of 40 single-tile guard moves) and matches all 462
commands; the cleared path (`state[0x6a] != 0`) is a 32-command thief walk
with sub-event 27.  `mevent` floor 10 (`idou`, tile cleared, `ichicheck`)
is routed from the trigger tile (5,4).  `yousei` (`0x478f0c`) computes the
area score and rank and writes `save0.dat`; the oracle got typed contracts
for the Delphi file routines (BlockRead yields zeros, i.e. no valid
`optionN.dat`, so the built-in temporary ranking is used, as the remake
always does) and for `TCustomForm.ShowModal` (`0x4237dc`).  Button1Click
`0x1a` on difficulty 0 shows the ranking form and does **not** restart
Timer2: the rest of the fairy's timeline (the gate at 0x1b opening, sound
2) waits for the next `timer2on`.  The drain models this: Timer2 has its own
object, `TTimer.SetEnabled` on it is tracked, and a confirm that leaves it
off with commands left stops as `timer_off`; the engine no longer locks
input for a queue whose Timer2 is off.  84 of 84 witnesses match builder and
drain parity.

**moncheck** (`0x4664e4`, splice `0x0b/5` at the end of every battle
timeline) is ported for every floor case (§12a).  The remake fights in C (`src/game.c`, checked by
`test_game_parity`); after a won battle the engine ran the `moncheck`
builder with Timer2 counted as running.  Since every move runs
`builders.handan` (§12c), battles are `taisen` timelines whose `0x0b/5`
splice runs `moncheck`, and that bridge is gone.  This
completes floor 10: the soldiers' deaths open the gate (sub-event 24), the
Golden Knight's death places the rewards and the fairy (sub-event 25).

**Altars** (`handan` case `0x10`): the offer is a Yes/No question in
`choice_mode` 1; `Button2Click` then shows the four-option menu
(`Button39..42`).  8 witnesses (offers at n = 1 and 3, the menu, each
option incl. area 2 / option A, too little gold, cancel) match builder and
drain parity (136 strict drains overall).  The drain leaves the open menu unanswered
like a Yes/No question.

P3b is complete.  Event-driven battles (`taisen2`) are a self-driving
timeline (section 11); message IDs are in `18-messages.md`.  Drains run
strict (untainted); `--permissive-vcl` remains for exploring new handlers,
and the `permissive_targets` list in a report shows which VCL calls still
need a typed contract.

## 9. Coverage baseline (P4 gate)

`export_event_vectors.py` now stores `executed_runs`, the contiguous executed
address runs in execution order.  `check_event_coverage.py` maps those runs to
the static CFG and reports, per handler region, covered blocks, covered
static edges and executed command-opcode stores (append sites).  Region
reachability follows branches, decoded jump tables and in-region direct
calls; data ranges and unreachable padding are excluded.  A block that ends
in a call reaches its fall-through block only after the callee returns, so
that edge is counted when a run starts at the return address after the
calling block ran (the callee's blocks sit between the two in the trace).

Matrix after the completed `ichicheck` batch (`action_*` vectors are
checked by `test_game_parity` instead; re-run the checker for current
figures):

| Region | Blocks | Edges | Append sites |
|---|---:|---:|---:|
| `Button1Click` | 25/28 | 38/47 | 14/14 |
| `Timer1Timer` | 0/46 | 0/73 | 0/0 |
| `banpaiawork` | 0/10 | 0/13 | 0/0 |
| `doorstackin` | 1/1 | 0/0 | 32/32 |
| `gameover` | 0/5 | 0/6 | 0/0 |
| `handan` | 49/91 | 65/145 | 30/362 |
| `ichicheck` | 69/69 | 106/111 | 1111/1111 |
| `madoushi1` | 17/19 | 22/26 | 8/8 |
| `madoushi2` | 1/1 | 0/0 | 128/128 |
| `madoushilivework` | 1/1 | 0/0 | 42/42 |
| `mevent` | 25/30 | 36/52 | 5/15 |
| `moncheck` | 85/85 | 152/152 | 642/642 |
| `openning2` | 16/27 | 15/38 | 129/129 |
| `openning31` | 12/13 | 14/16 | 12/12 |
| `openning9` | 0/97 | 0/160 | 0/0 |
| `roujin` | 22/41 | 30/70 | 18/22 |
| `syounin` | 39/56 | 50/86 | 0/0 |
| `taisen2` | 23/26 | 27/37 | 50/50 |
| `touzoku` | 30/33 | 41/47 | 38/38 |
| `yousei` | 134/242 | 159/378 | 10/10 |
| `zenolivework` | 1/1 | 0/0 | 36/36 |

This is the P4 map, not a completion claim.  `handan` and `ichicheck` are
large dispatchers: the covered blocks are the raw-tile/floor cases with
witnesses, and the uncovered rows are the next witness targets.  The fully
covered rows (`doorstackin`, `madoushilivework`, `zenolivework`) are the first
regions safe to put behind `--require`, together with `ichicheck`'s blocks
and append sites.  The launch intro (`timer4ontimer`) is
presentation, implemented in `src/intro.c` and tested by `tests/test_intro.c`;
it is not part of the event-vector coverage matrix.

```sh
python3 docs/re/tools/check_event_coverage.py --details
python3 docs/re/tools/check_event_coverage.py --require ichicheck
```

The checker fails closed: a missing vector directory, an unpinned binary, an
unknown `--require` region or an incomplete required region returns non-zero.



## 10. Launch intro (`open0.bmp`)

Recovered from `timer4ontimer` (`0x482a98`) and implemented in
`src/intro.c` (timing/geometry), `src/assets.c` (frames) and `src/render.c`
(draw):

* `syokidata0` fills `ImageList1` from the Chtable atlas in row-major order
  (`0x487d70..0x487e1d`), so image index `n` is Chtable tile `n + 1`.
  The timer uses sprite tiles 217/218 with silhouette masks 210/212.
* 20 ticks, alternating the two frames.  Each frame is drawn at
  `(304 + tick, 368 + tick)` with size `32 - tick` for 640x400; the composite
  runs for ticks 0..18, so tick 19 is background-only.
* Intervals are 350 ms (ticks 0..7), 250 ms (8..14), 150 ms (15..19); after
  tick 19 the intro ends.  The original then clears `opt_difficulty` /
  `opt_carry` and runs `gameover`; the remake already has the initial game
  state, so it simply returns to the playfield.
* The status bar (`Edit8`) keeps its DFM default text during the intro:
  nothing writes the bar until `timer4ontimer`'s final `gameover` →
  `syokidata2` → `disp`.  `tools/extract_messages.py` writes that text to
  `data/text/en/ui.tsv` (index 0) and the remake shows it in the console under
  the intro picture.
* At launch the remake starts the intro only when no save was loaded.
  `--no-intro` skips it for deterministic screenshots/replays; unlike the
  original, gameplay input cannot skip it.  Loading a save never starts it.
* `tests/test_intro.c` checks the interval boundaries, the 20-tick finish,
  frame parity and the shrinking geometry.

## 11. Event-driven battles (`taisen2`)

Recovered from `taisen2` (`0x47f7d4`, timeline sub-dispatch `0xb/10`) and
implemented as `builders.taisen2`:

* The `0xb` command's `arg2` is the target map index; the engine now sets
  `aux.target_index` from it before the splice builder runs.
* The tile is decoded with the same two ranges as `mmes`:
  `0x3d..0x5f -> ((raw+1)>>1)-31`, `0x85..0x9e -> ((raw+1)>>1)-47`.
  Monster HP/attack/defense/gold come from `MONSTER_TABLE` via the new
  `monster_stat(index, field)` builder API.
* `dameiji2` is the turn recursion
  `hp = defense*(diff+1) + (hp - attack)`; a non-positive damage step would
  recurse forever, so the builder fails closed.  The winning branch appends
  6 + 12*turns commands: `0xb/5/5` (moncheck), gold, sprite, player tile,
  `0xb/9`, then one damage/hit/sound group per turn.
* The losing branch (`battle_damage >= life`) uses `ydamei3` and appends
  21 + 5 + 12*(turns-1) commands, leaving `state[0xc]` (battle render flag)
  set.  The winning branch clears it.
* `aux.work_558` is the turn number, `aux.current_monster` /
  `aux.event_selector` are the monster index, `aux.monster_hp` is
  `hp*(diff+1)`, and `aux.battle_turns` is the final turn count.
* The battle is a self-driving command timeline, not a modal choice: Timer2
  and the existing event lock are the suspension.  There is no extra input
  contract to model.

Golden vectors: `taisen2_win`, `taisen2_lose`, `taisen2_win3`,
`taisen2_lose4`, `taisen2_master` and `taisen2_win_drain` (1-, 3- and
4-turn paths, both branches, both monster ranges).  Builder/state/map/aux
parity passes for all six.  `taisen2_win_drain` runs on floor 10, where the
post-battle `moncheck` is ported, and the strict drain completes (18 steps,
gold +2, the player tile restore).  The floor-40 wins also drain through the ported `moncheck` case.
The losing branch drains too (`taisen2_lose`, `taisen2_lose4`: answer Yes;
`taisen2_lose_no`: answer No).  The game-over path, ported in
`event_vm.c`/`event_engine.c`:

* the timeline's op `0x03` (and ops `0x10`/`0x13` when life reaches 0, which
  first set the cursor to 1) is the death window: BGM `state[0x5a]` becomes
  `0x0e` when on, `state[0xc]` = 1, `g_subevent_id` 9, `g_dialog_selector`
  `0x1e`, then `zyouout`.  With id 9 `zyouout` shows nothing: it appends the
  game-over timeline (`event_vm_death_timeline`: `0`, `0x16`, five `0`,
  `0x0a/0x5d`, four `0`, plus nine `0` and `0x01/5` with BGM on) and calls
  `timer2on`.  As with every append in a step, the common tail skips the
  last command appended;
* op `0x0a/0x5d` opens sub-event 103; `zyouout` (id ≥ 10, selector `0x5d`,
  `state[0xa7]` ≠ 1) asks with `MessageDlg` (`0x42cec8`, called at
  `0x44a3b8`, literal at `0x44a454`, `data/text/en/ui.tsv` entry 1) instead of
  showing the window;
* Yes (`mrYes` = 6) calls `timer2on`; the timeline reaches op `0x16`
  (`0x4547d3`), which clears both option words and calls `gameover`:
  `syokidata` (`state_syokidata` plus the embedded initial map, i.e.
  `data/maps/initial.bin`), `syokidata2` (`g_tl_count` = 0, so the common
  tail leaves -1; `g_subevent_id` 0, `g_move_dir` 1, `g_anim_sprite` 0,
  windows hidden), `itemlive` and Timer1 on.  Sub-dispatch 6 (`0x4533fb`)
  is the ending's variant of the same reset, not this path;
* No enables Game > Load (`TMenuItem.SetEnabled`, form `+0x334`); the
  timeline stays stopped with life 0 and the death map stays on screen.
  The remake does the same: the view stays on the map, the dead hero is
  inert, and the menu (with Load) is the way out.

The oracle answers per witness (`Witness.message_dlg`, `.vec` line
`drain_message_dlg`), and the drain's taisen2 battles now also bind the
monster to the VM (`event_builders_bind_monster`), so ops `0x04`/`0x05`
take life and HP in the engine as well.

## 12a. `moncheck`, all floor cases

`moncheck`'s floor switch (index table `0x466512`, targets `0x466545`) has
22 cases; Ghidra's decompiler times out on the function, so the cases were
read from the raw disassembly.  Every case sums a few current-floor tiles
(monsters gone = floor 6) and compares the total; the bodies are straight
command appends plus `doorstackin` (`builders.lua` `MONCHECK[n]`).  Witnesses
`moncheck_floor*` (60 vectors, every block and every edge): builder, state,
map and aux parity and strict drains for all.  Besides each effect they
cover each guard failing (`*_not`), the one-shot flags already set
(`*_done`), the multi-group floors with only some groups cleared (17, 45),
floor 49's ring cleared with a Sergeant gone, the sound-off variants, and
floor 51 past the switch.

| Floor | Case | Guard (tiles → total) | Effect |
|---:|---|---|---|
| 2 | `0x4665a1` | 0x10, 0x12, 0x30 → 0x11 | six doors (kind 6) |
| 8 | `0x466814` | 0x34, 0x36, 0x2a → 0x10 | door 0x2a |
| 10 | `0x466901` | §10 | gate + sub-event 24; knight → rewards, sub-event 25 |
| 11 | `0x46763c` | 0x2c, 0x2e, 0x22 → 0x10 | door 0x22 |
| 14 | `0x467729` | 0x00, 0x02, 0x0c, 0x16 → 0x1a | tile 0x16 → 0x1e |
| 15 | `0x46783e` | 0x3c, 0x1b, 0x5d → 0x0e | tile 0x31 → 0x29, doors 0x5d, 0x1b |
| 17 | `0x467a3b` | four groups → 0x10 each | doors 0x2a, 0x4b, 0x22, 0x43 independently |
| 20 | `0x467df8` | 11 tiles → 0x3e | stairs at 5, rewards, fairy at 0x31, sound 6, sub-event 31, doors 0x5d, 0x1b |
| 25 | `0x46887e` | 0x3c, 0x68 → 0x0a | with `state[0x74]` = 1 floor-24 cells 4/5/6/16/27/38 builder-side; rewards, fairy 0x47, door 0x68 |
| 30 | `0x468d0f` | 8 tiles → 0x2c | stairs at 5, doors 0x47, 0x26 |
| 32 | `0x468f4d` | 0x63, 0x65, 0x59 → 0x10; then tile 0x3c + `state[0x78]` + `state[0x79]` → 7 | door 0x59; then Button1 "OK", floor-29 cell 17 = 6 and floor-2 cell 120 = 0x15 builder-side, 13 `0x0c` moves 9 → 0x68, `0x11` 0x68/0x85, sub-event 41 |
| 33 | `0x469a08` | 6 tiles → 0x20 | doors 0x56, 0x2a |
| 34 | `0x469b98` | 8 tiles → 0x30, one-shot `state[0x94]` | five reward tiles |
| 35 | `0x469f53` | 0x47, 0x1b → 0x0a, one-shot `state[0x95]` | four tiles, door 0x1b |
| 38 | `0x46a2a2` | 0x63, 0x65 → 0x0c | builder-side tile 0x59 = 9, no commands |
| 40 | `0x46a2d3` | 0x10, 0x05 → 0x11, one-shot `state[0x96]` | 13 reward tiles |
| 41 | `0x46ab37` | 0x0c, 0x14 → 0x0c, one-shot `state[0x9a]` | tile 0x31 → 0x2d, five tiles with sound 1 |
| 44 | `0x46af38` | 0x5c, 0x5e, 0x52 → 0x10 | door 0x52 |
| 45 | `0x46b02b` | two groups → 0x10 | doors 0x66, 0x69 |
| 48 | `0x46b20c` | 0x00, 0x54 → 0x0a | door 0x54 |
| 49 | `0x46b2f2` | see below | doors 0x5d, 0x47; Magic Sergeants; Zeno |
| 50 | `0x46bfa8` | option A (`state[0x9f]`) ≠ 0 | `timer2on`, the ending timeline (sub-dispatch 6 runs last), `timer2on` |

Floor 49: the ring 0x10/0x1a/0x1c/0x26 → 0x18 with the four Magic Sergeants
(`0x5d` at 0x0f/0x11/0x25/0x27, sum 0x174) sets one-shot `state[0x9c]`
(Zeno's stats then scale with the difficulty in `syokidata2`; the three
dwords `0x489a00..08` are divided by 10 — not modelled, outside the state
block) and opens sub-event 56 with sounds 5/6.  Zeno gone (0x1b, 0x47, 0x3c
→ 0x10) sets `g_tl_count` = 0 first — the rest of the battle timeline is
dropped — then places the rewards, opens door 0x47 and clears the four
sergeant cells.

Sub-dispatch 6 (the floor-50 ending, `0x4533fb`) now runs `gameover` in the
engine like op `0x16`, after the VM set option A to `0x2b` (or option B to 1
on a carried run).

## 12b. `idou`'s movement attacks

`idou` (`0x44c04c`) ends every step that lands the player on a cell
(`handan` floor, key and item cases, and the map events that move him).
After the move it probes the four neighbours with `mmes` (up, left, right,
down; each probe only inside the map, `g_battle_damage` = −22, −2, 2, 22):

* `mazyutu1` (`0x44c5c4`): a magician (`mmes` sprite `0x6e` for raw
  `0x97`/`0x98`, `0x6f` for `0x99`/`0x9a`).  On floor 47 a `0x6e` magician
  is first pushed one cell further when that cell is floor (`0x0c` from
  p + d/2 to p + d, sound 2; the check tests y in 1..9 whatever the
  direction and reads floor 47's base + p + d, so p + d < 0 reads floor
  46).  The remake bounds this push to a real one-cell step inside floor
  47, so a magician at an edge stays put instead of being written into
  floor 46 or wrapping to the opposite side (`08-remake.md` decision 19).
  Without the Sacred Shield: op `0x10` −200 (`0x6e`) or −100 (`0x6f`)
  × (difficulty + 1), sprite `0x6e`/`0x6f` (+`0x1c` when difficulty ≠ 0),
  the hit pose (`0x00 6/{bf,c7,cf,d7}` + `0x09 p/{c0,c8,d0,d8}` by facing)
  and sound 1.  Then `timer2on`, sprite 0, `DAT_0048c560` = 1.  Each
  adjacent magician strikes.
* `madoushi1` (`0x44c464`), without the Sacred Shield, between two Magic
  Sergeants (`0x5d` left and right with x in 1..9, or above and below with y
  in 1..9; each pincer runs it): op `0x13` (life halved), sprite `0x70`, the
  hit pose, sound 1, `timer2on`.
* With neither (`DAT_0048c560` = 0) the neighbours are probed again and
  `disp` redraws.  `mmes` also sets `state[0xa6]` next to stairs on the top
  floor.

`builders.idou` runs the move and `builders.idou_checks`; the engine runs
`idou_checks` alone after every ordinary step of the C movement code
(`event_engine_after_step`) until every move went through
`builders.handan` (§12c), which made that bridge unnecessary.  A strike to 0 life runs
the death window (§11).  Witnesses `idou_*` (24): each probe direction,
both magician kinds, difficulty, two magicians, the Sacred Shield, the map
edges, the floor-47 push (up, down, blocked, not for `0x6f`), both pincers,
every facing, a magician with a pincer, and a fatal strike that drains
through the try-again reset.  The two uncovered `madoushi1` blocks are the
"facing not 1..4" fall-throughs, unreachable in play.

## 12c. Every `handan` case

`builders.handan` (`0x444490`) now covers every raw value; the per-tile
`action_*` vectors (the reference for the C movement code) run through it
with builder, state, map, aux and drain parity.

| Raw | Behaviour |
|---|---|
| `0x01..0x03` | doors: a key opens it (sprite 4/5/6, item panel, door animation, sound 1), else the locked sprite `0x10..0x12`; `doorcheck` (`0x47beac`) first queues floor 39's wall (cells 12/16/34/36/56/58/60 all doors, 14 or 38 clear, target `0x26`/`0x0e`) or floor 43's `0x06 7/7` next to a Magic Sergeant |
| `0x0a` | hidden passages: floors 12/16/19 at one cell, floor 41 a magician at `0x14` once cell 12 is floor |
| `0x0b`/`0x0c` | stairs: 43↔45, one-shot cells on floors 20/25/40/49, the floor limit (with the floor-2 cell rule) and the floor-29 rewrite, arrival at map-block byte `floor·0x7b` (+1), facing back to the stairs, floors 32/42's `mevent` once, `kaidanwork`'s two-pass `0x15` wipe (BGM at step 20) at the stairs speed |
| `0x1b`/`0x1c` | BGM 13, the floor-24 cells, floor-50 cell 49 = `0x15`, tile `0x3c` → `0x1b`, sub-events 36 and 35 |
| `0x1d..0x1f` | keys |
| `0x20..0x32` | potions and gems (scaled by `(floor + 9) / 10` and difficulty; " point up!" is `ui.tsv` entry 2), the special items; `0x24` has no case |
| `0x33..0x3c` | swords and shields |
| monsters | `taisen` (below) |
| `0x5b`/`0x5c` | Zeno: battle on floor 49 and on a later round of floor 50 (1000/625/125 unless carried over); the 1098-command ending on the first round |
| others | nothing (`0x00`, `0x04`, `0x05`, `0x08`, `0x0d..0x0f`, `0x11`, `0x12`) |

`taisen` (`0x44a480`): the monster index from the raw (`0x3d..0x5f`,
`0x61..0x69` → 18, `0x73..0x7b` → 19, `0x84..0x9d`; `0x6a..0x72` would read
a stale `g_anim_phase` and are on no map), `idou` onto the monster's cell
(with idou's attack checks), `dameiji` (Cross / Dragon Slayer doubling;
damage = (attack − defense) × (turns − 1)); a win queues `moncheck`, the
gold (halved for the octopus' outer cells), the cleared cell(s) — nine at
the octopus' and the dragon's centres, whose first hit uses op `0x0e` —
and the hit animations; a loss queues the death window; a monster whose
defense blocks every attack only shows `ListBox2[0xd9]`.  `timer2on` runs
`itemdel` first, so it clears the item-panel flag.

Play now goes through it: an arrow key does what `formkeydown`
(`0x460bd8`) does -- ignored while a timeline runs (`g_tl_count != 0`, a
count left at −1 by `gameover` reads as 0), turn the hero, and unless he
faces the floor's edge run `handan` on the cell ahead.  `src/game.c` keeps
only the state helpers (new game, load, snapshot); the replay tool runs the
same engine headless.

`mevent` also has floor 23 (the wall at the target); floors without a case
do nothing.  `Timer1Timer` (500 ms) counts visits to the octopus' and the
dragon's cells (`state[0x98]`/`[0x99]`, at most 2; `takowork1/2` only
repaint the back buffer).  Witnesses: `handan_*` (passages, `0x1b`,
doorcheck, inert raws), `battle_*` (20), `stairs_*` (13), `mevent_floor23`.
The oracle now captures up to 4096 commands (the VM capacity).

## 12d. Item use

Enter on the inventory runs what `formkeydown` (`0x460bd8`) runs for the
selected slot on the item overlay; all of it is in `builders.lua` with
builder, state, map, aux and drain parity (witnesses `item_*`, `fly_*`;
new aux `confirm_case` = `0x48c574`).

| Handler | Slot | Behaviour |
|---|---|---|
| `img6work` `0x480f60` | 0 Orb of the Hero | value 1 only: Timer1 off, `yousya`/`obudameiji` rows (≤ 7 kinds, map order) |
| `img5work` `0x48198c` | 1 Orb of Wisdom | `g_subevent_id = 1`, the learned-words window (flags == 2) |
| `img4work` `0x481e80` | 2 Orb of Flying | clears `state[0x9e]`/`[0xa6]`; next to stairs (`mmes` sprite `0x13`): flight panel, `state[0xc] = 1`; else sprite `0x14` |
| `img8work`..`img17work` | 4..9, 11..13 | item held (== 1; the Warp Staff >= 1): `itemdel`, confirm case 5..14 (no 11), Yes/No panel |
| `Button37Click` `0x450b78` | | No: `itemlive` |
| `Button38Click` `0x450ba0` | | Yes, by case: 5 elixir, 6 mattock (`matokwork`), 7 bomb (`toroad2`, gold, status `0xbc 0xc8 " " n 0xc9`), 8 warp, 9/10 wings, 12 crystal (`yukiwork`, status `0xc0 0xc6`; the remake inserts a space, `08-remake.md` decision 20), 13 key, 14 super mattock |
| `Button8/9Click` | | fly up / down one floor (43↔45), `mhyouji` |
| `Button10Click` `0x44eaf4` | | land: fairy cells on 25/49, direction flip on the highest floor, arrival byte `floor·0x7b + d − 1`, floor-29 rewrite, `kaidanup/downcheck` facing, sound 2 |

The neighbour scans visit down, right, left, up; `aux.event_selector`
is set for every neighbour, the target index only on the board (the up
neighbour's always).  The effects themselves are in
`10-events-and-items.md` (Effects).  Coverage: every block of `Button38Click`,
`Button10Click`, `img4work` and `img5work`; what is left in `img6work`,
`img8work`.., `mmmes`, `yousya`, `obudameiji` and the `Button8/9Click`
visibility tests is layout-option code (`g_option_dat`), the signed
rounding of non-negative values, switch defaults and VCL object state,
none of which a game-state witness can reach.

## 12e. Drains everywhere; Zeno's table entry; `openning9`

All witnesses now drain (the execution oracle's drain, compared in
`test_event_builders`), except four floor-40 ones on a synthetic map, whose
monster waves carry raw 0 into the battle cell (`taisen2` then reads a
stale `g_anim_phase`).  `handan_floor40_real` runs floor 40 as shipped
(`Witness.initial_floor`, from `data/maps/initial.bin`).

* `0x0b/0xb` (`0x45362b`): `g_battle_turns = 0`, floor = arg2, `mhyouji`
  (clears `state[0xa6]`), `disp`; on floor 50 also `state[0xc] = 0`, the
  player at the floor's up-arrival byte, facing 4.
* Zeno's entry `g_monsters[0x0f]` (`0x489a00..08`) is rewritten while the
  game runs, so it lives in the VM (`EventVm.zeno`, `set_zeno`):
  `syokidata2` on load/new game/game over (state `0x9c` = 0: 8000/5000/1000,
  1: (difficulty + 1) × 800/500/100), `handan` on floor 50 of a later round
  (1000/625/125 unless carried over, `0x4463b3`), the floor-49 one-shot
  (÷ 10, `0x46b566`) and the floor-24 one (× 10, then 1000/625/125 on a
  later uncarried round, `0x470516`).  The oracle runs no `syokidata2`, so
  the parity test starts from the shipped entry.
* The execution oracle now stops a drain when a step turns Timer2 off
  outside op `0x0a` (after the tick's second call): `openning9` phase 8
  (the ending's buttons).  `0x41a784` (draw a graphic on a canvas) is a
  classified presentation stub.
* Coverage counts the drain too: each stackwork/Button1Click call is its
  own trace (`drain.executed_segments`).  `openning9` is at 66/97 blocks;
  the rest are option.dat speed and 40-px layout variants, VCL visibility
  tests and phase 2's `0xf8` frame, which no ported handler queues.

## 12. `ichicheck` floor batches (all fourteen floors)

Ported in `builders.lua`; entries are in the original `ichicheck` table.
The first five are straight-line cases with no state guards:

| Floor | Handler | Commands | Contents |
|---:|---|---:|---|
| 15 | `0x46f82e` | 36 | `doorstackin(0x5d, 5)`, sound 1/5 |
| 25 | `0x4727b2` | 38 | status text 0xda, sub-event 0x17, `doorstackin(0x68, 5)`, sound 1 |
| 30 | `0x4728ef` | 70 | `doorstackin(0x47, 5)` and `doorstackin(0x26, 5)`, sound 1 each |
| 38 | `0x47382c` | 39 | sprite 0x72, `doorstackin(0x2d, 8)`, sound 1 |
| 48 | `0x476cdd` | 6 | tile 0 -> 0x5d, sound 2 |

`mevent` now routes these floors: 15/30/38 run `idou`, clear the player tile
and call `ichicheck`; 25 also sets BGM state 7 when sound is on; 48 calls
`ichicheck` only.  `handan` raw `0x07` is routed for the same floor set.

Witnesses: direct, `mevent` and `handan` cases for each floor.  The same
batch also ports floors 20, 32, 33, 42, 43 and 49: 20 uses 32
`banpaiawork` stages, 32 runs `taisen2` before 13 monster moves, 33 has a
player-index guard (`0x6b`) and two door paths, 42 has four sub-events plus
`madoushilivework`/`zenolivework`, 43 is gated on tile 8 == `0x5d`, and 49
uses `madoushi2` plus `zenolivework`.  Floors 24 and 40 are also ported:
24 has the option-A/B `taisen2` branch, eight dialogue sub-events and the
173-command animation; 40 has five `taisen2` waves, thirteen one-shot trap
tiles and two doors.  All fourteen `ichicheck` floor cases are now ported,
and the coverage checker reports **69/69 blocks and 1111/1111 append sites**
(106/111 edges; the rest are common-exit paths).

`madoushi2` (`0x462560`, splice `0x0b/3`, floor 49) is ported: it puts the
Magic Sergeant on the eight cells around `0x1b` and queues eight mask
frames (`0xb6 .. 0xa7`) for each, 128 commands.  It then increments the
count once more without writing, so the stale slot above the last command
is consumed first; the native oracle confirms it (129 command words, the
last one whatever the buffer held).  The builder mirrors this with
`set_count(get_count() + 1)`.  The four floor-49 witnesses now drain end to
end (1/1 blocks, 128/128 append sites).
