# Static findings from the Ghidra annotation pass

Status: **static only**.  Everything here was read from the disassembly of
the pinned `TSW.exe` while annotating the Ghidra project (see
`01-binary-and-assets.md`, Tooling notes).  Nothing was traced at runtime.
Items marked *candidate* or *inferred* still need an oracle case or a trace.

## 1. `openning31` is the post-Zeno demotion, not a new-game reset

`openning31` (`0x4653b8`, timeline `0x0b` sub-event 1) rewrites the state
before queuing its timeline:

| Field | Option A (`0x4b8904`) = 0 | Option A ≠ 0 |
|---|---|---|
| floor / highest floor | 2 / 2 | 2 / 2 |
| x, y | 2, 7 | 2, 7 |
| life / attack / defense | 400 / 10 / 10 | unchanged |
| `0x4b86b8` (state `0x0c`) | 1 | 1 |
| weapon / shield display level | 0 / 0 | 0 / 0 |
| Sacred sword / shield picked | 0 / 0 | 0 / 0 |
| special item slots 0..14 | unchanged | all 0 |

If `0x4b87f0` is set it first stops the media object at form `+0x2d8`.  It
loads three panel images from the data directory, then appends (in this
order) `{0}`, `{0x0b,8}`, `{0}`, `{0x0a,3}`, `{0}`, `{0x0a,2}`, `{0}`,
`{0x0a,1}`, `{0}`, `{0x0a,1}`, `{0x08}`, `{0x02,0}`.  Because the timeline is
LIFO (`12-event-vm.md`), this **runs** as: keep sprite, show item/key
panels, choice/dialogue sub-events 11, 11, 12, 13, then sub-dispatch 8
(`yusyahyouji`, player/map update).

Consequence for `03-state-and-save.md`: the new-game state from `syokidata`
(1000/100/100 with the Sacred sword and shield shown as picked) is the
**opening segment** before Zeno, not a cosmetic quirk.  The demotion to
400/10/10 without the Sacred gear happens here, on floor 2 at (2, 7).

## 2. Choice-window confirm writes event flags (candidate flag setter)

* Timeline op `0x0A` stores `arg1 + 10` to `DAT_0048c5a4` (code at
  `0x4531a8`) before calling `zyouout`.  Other writers store fixed ids
  (e.g. `9` at `0x452d99`, `0x45417f`, `0x4542f5`, together with
  `state[0x0c] = 1`).  Call it the **sub-event id**; it is distinct from
  `DAT_0048c5a0` (`g_dialog_selector`).
* `Button1Click` (`0x44da10`, the choice window's confirm button) begins with:

  ```
  if 0x0a < subId < 0x44:  dword[0x4b87c8 + subId*4] = 1   ; state[0x50 + subId]
  ```

  then dispatches on `subId` (`< 0x1b` table `0x44da4f`, `0x1b..0x32` table
  `0x44dad3`, then compares).
* So a choice opened by op `0x0A` with `arg1 = a` sets
  **`state[0x5a + a]`** when confirmed.  This writes the flag region
  `0x5b..0x93` without any direct `mov` to the flag address, which explains
  why xrefs show no setter for several flags.  *Candidate:* the floor-10
  guard `state[0x6a]` (`15-event-flags.md`) would be set by confirming
  sub-event id `0x1a` (op `0x0A` with `arg1 = 0x10`).  **Confirmed** by the
  P3b execution oracle: `yousei` appends `0x0a/0x10`, and confirming
  sub-event 26 writes `state[0x6a] = 1` at `0x44da2a`
  (`16-event-pilot.md` §8).  The window is not a choice-only window: it is
  the dialogue OK for every op-`0x0A` sub-event (op `0x0A` also disables
  Timer2, which `Button1Click` re-enables through `timer2on`), and case 20
  (`0x44e022`) is the Zeno-dialogue continuation that calls `openning2`.

`Button11Click` … `Button35Click` are the choice buttons: each stores
`N - 11` to `g_dialog_selector` (`0x48c5a0`), sets `0x4b8924` (state
`0xa7`, listed as "unknown metadata") to 1, and calls `zyouout`.
(Values were read for 11, 14, 15, 17, 18, 20, 22, 23, 25, 27, 29, 30, 32,
33, 34; the rest follow the same 26-byte pattern.)

## 3. Launch intro (`open0.bmp`) = `timer4ontimer`, gated by `K5`

**Recovered and implemented** in `src/intro.c` / `src/assets.c` / `src/render.c`.

* `formactivate` (`0x44280c`) enables the timer at form `+0x41c` (and resets
  the frame counter `0x48c56c`) only when the check item at form `+0x420` is
  checked.  That item is toggled by `K5Click` (`0x484a20`), which mirrors it
  into `option.dat` byte 5 (`0x489ba1`, default 1 in `NormalOption1Click`).
* `timer4ontimer` (`0x482a98`) is the intro: each tick it loads
  `<data dir>\open0.bmp` (string at `0x482e54`), alternates two sprite pairs
  (`0xd8`/`0xd1` on even ticks, `0xd9`/`0xd3` on odd ticks), and draws the
  pair through a shrinking destination rectangle.
* `syokidata0` (`0x487d70..0x487e1d`) fills `ImageList1` from the Chtable
  atlas in row-major order (20 rows × 11 columns).  Image index `n` is
  therefore Chtable tile `n + 1`: `0xd8`/`0xd9` are sprite tiles 217/218 and
  `0xd1`/`0xd3` are the matching silhouette masks 210/212.
* The destination rectangle is `(304 + tick, 368 + tick)` with size
  `32 - tick` for the 640×400 layout (the 800×500 layout uses
  `(380 + tick, 460 + tick)`, size `40 - tick`).  `kasanegaki` composites the
  mask/sprite pair for ticks 0..18; tick 19 is background-only.  The remake
  pre-composites both frames to RGBA.
* Timer interval: 350 ms for ticks 0..7, 250 ms for 8..14, 150 ms for
  15..19.  After tick 19 the timer stops, `opt_difficulty`/`opt_carry`
  (`0x4b8904`/`0x4b8908`) are cleared, and `gameover` starts the new game.
* The load handlers read so far (`Load21Click`, `Load41Click`,
  `Load51Click`, `Load71Click`; the others have the same layout) disable
  this timer if it is running, so loading a save skips the intro.
* The remake starts the intro once at launch when no save was loaded,
  renders it over the 640×400 playfield, and skips it only with the
  deterministic `--no-intro` switch.  Gameplay input cannot skip it, as in
  the original.  A loaded save never starts it.

The frame counter `0x48c56c` is the same global that `dameiji` uses as
`g_battle_turns`; it is reused, not a second variable.

## 4. Raw tile → monster index (from `mmmes`, `0x451c20`)

`mmmes` classifies the tile at map index `g_event_selector` on the current
floor: raw `0x08` → `g_anim_sprite = 1`, raw `0x0d` → 3, monster tiles → 2
and `0x48c5b8` (current monster index) is set:

| Raw tiles | Monster index | Classified as monster by `mmmes` |
|---|---|---|
| `0x3d..0x60` | `(raw - 0x3d) >> 1` (0..17) | yes, except `0x5b..0x5c` (Zeno) |
| `0x61..0x72` | 18 (Giant Octopus) | no |
| `0x73..0x84` | 19 (Dragon) | no |
| `0x85..0x9e` | `((raw - 0x85) >> 1) + 20` (20..32) | yes, except `0x91..0x92` (Great Magic Master) |

This extends the normal-encounter formula in `06-data-tables.md`
(`((raw+1)>>1) - 31`, identical for `0x3d..0x5a`) to all 33 records; the
multi-tile bosses (18, 19) and the scripted bosses (15, 26) are handled by
event code.

## 5. Battle forecast used by `taisen2`

`dameiji2` (`0x48028c`) is the forecast for `taisen2` (sub-event 10).  It
uses the same HP/turn/damage model as `dameiji`, but **without** the
Cross / Dragon Slayer ×2 multiplier and **without** the unwinnable check.
Its turn recursion `damei3` (`0x4802fc`) is
`hp += monsterDef*(diff+1) - attack` until `hp <= 0`, which would not
terminate if `monsterDef*(diff+1) >= attack`; scripted battles must avoid
that case.  `ydamei3` (`0x480340`) is the mirrored recursion
(`hp += defense - monsterAtk*(diff+1)`), used by the losing branch.

Ported as `builders.taisen2`.  The 640×400/800×500 `monshyouji` UI is
presentation; the engine only needs the monster table (new
`monster_stat(index, field)` API) and the command stream.  The winning
branch appends `0xb/5/5` (moncheck), gold, sprite, the player-tile restore and
one 12-command hit group per turn; the losing branch appends the 21-command
death setup plus the mirrored groups and leaves `state[0xc]` set.  Golden
vectors `taisen2_*` cover both branches, both raw ranges and 1/3/4-turn
loops (`16-event-pilot.md` §11).

The floor-40 post-battle case (`moncheck` jump target `0x46a2d3`) checks
`tile[5] + tile[16] == 0x11`, sets the one-shot `state[0x96]`, then appends
13 tile writes (`0x29/0x28/0x27 -> 0x1d`, `0x25/0x24/0x23 -> 0x21`,
`0x14/0x13/0x12 -> 0x23`, `0x0e/0x0d/0x0c -> 0x22`, `0x3c -> 0x17`) with
sound 2, plus sound 6 when the sound state is on.  Ported as `MONCHECK[40]`.

## 6. Timeline builders (examples of the LIFO order)

All of these pre-increment `g_tl_count` and write `TlCmd` records, so the
last appended command runs first.

* `toroad2` (`0x44bc34`): calls `timer2on`, appends a silent tile set
  `{0x11, target, 6}` first, then five groups `{0, raw, 6}`,
  `{0x09, target, s}`, `{0}`, `{0x01, 1}` with `s = 0xb6, 0xb2, 0xae, 0xaa,
  0xa6`.  It runs as five frames `0xa6 → 0xb6` (each with sound 1), and the
  tile becomes floor last.  The `{0, raw, 6}` record supplies the "previous
  command" arguments that op `0x09` reads as its third/fourth value.
* `matokwork` (`0x451b98`): `{0}`, `{0x0d, g_event_selector, 7}`, then
  `doorstackin`, then `{0}`, `{0x01, 1}` (door animation kind 7).
* `yukiwork` (`0x451d3c`): `{0}`, `{0x11, g_event_selector, 6}`, `{0}`,
  `{0x0d, g_event_selector, word[0x48c564]}`, `doorstackin`, `{0}`,
  `{0x01, 1}`.  Note that `0x48c564` (`g_battle_unwinnable` in `dameiji`)
  is used here as the door-animation kind.
* `madoushi2` (`0x462560`, sub-event 3): writes tile `0x5d` (The Magic
  Sergeant) to the eight cells around map index `0x1b` (5, 2) on the current
  floor — indices `0x0f..0x11`, `0x1a`, `0x1c`, `0x25..0x27` — then appends
  a long appearance animation (`{0, 6, 0x5d}` + `{0x09, cell, frame}` pairs
  for frames `0xb6, 0xb3, 0xb1, 0xaf, …, 0xa9, 0xa7`).  `zenolivework` puts
  Zeno (`0x5b`) at index `0x1b` first, so this surrounds Zeno with eight
  sergeants.

## 7. Movement helpers

* `BitBtn1Click` … `BitBtn4Click` (on-screen arrows): only act when
  `g_tl_count <= 0`; they reset `g_tl_count` to 0, set `g_move_dir`
  (1 up, 2 left, 3 right), set facing `0x4b87e8` (up = 4, left = 2,
  right = 3), store the player index `y*11 + x` in `0x48c580`, compute the
  target (`-11`, `-1`, `+1`), and call `handan` if it stays on the board.
* `buttonfocus` (`0x442d50`) focuses the arrow button matching `g_move_dir`.
* `Button8Click` / `Button9Click` (`0x44ed94` / `0x44ed1c`, *inferred*
  Orb of Flying up/down): up requires `floor < highest floor`, down requires
  `floor > 1`; they set `0x4b8900` (state `0x9e`) to 1 (up) or 2 (down),
  skip floor 44 (43 ↔ 45), and redraw.  Flying up onto floor 35 sets tile
  `0x4b8aa4` to floor when `byte[0x4b9a62] + byte[0x4b8aa4] == 0x1b`.
* `Button10Click` (`0x44eaf4`) places the player after such a move: with
  `d = state[0x9e]` (1 or 2) it reads byte `d - 1` of the floor's stair
  pair (`0x4b8934 + floor*0x7b + d - 1`) and sets `x = b % 11`,
  `y = b / 11`; then `kaidanupcheck` (d = 1) or `kaidandowncheck` (d = 2),
  sound 2 and a redraw.  It also clears fairy tiles (`0x17`) on floor 25
  index 71 and floor 49 index 16 when `state[0x9e] != 0` and the player is
  not on that floor,
  and, when the highest floor is 29 and the current floor is below 29,
  rewrites floor-29 indices 38 and 27 to floor and 17 to `0x15`.
  `state[0x9e]` is therefore the last vertical move direction, and the stair
  pair stores the arrival index for each direction.

## 8. Menu facts

* `High1Click` / `Middle1Click` / `Low1Click` only call `speedhigh` /
  `speedmiddle` / `speedlow` (game speed); they do not touch the option
  flags.
* `GameQuit1Click` and `Exit1Click` free the six objects at
  `0x48c514..0x48c528` and terminate the application (`0x425018`).
* `About1Click` shows the form held in `0x48a760` modally.
* The option-byte writers are tabulated in `03-state-and-save.md`.
