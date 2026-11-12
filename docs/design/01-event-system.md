# Event system design: original VM and Lua builders

This document explains what the original event “VM” actually is and compares
the two practical ways to re-implement it: a TSV action table or embedded Lua.
**Decision (revised): the original command VM re-implemented in C, driven by
Lua builder scripts that emit the original commands** — see section 7, which
is the current model.  Sections 2–6 record the earlier TSV/Lua comparison and
the coroutine runtime that was built first and has since been removed.

## 1. What the original timeline VM is

The original game does not run event scripts as code.  Event handlers build a
**fixed command list** in memory; a small interpreter executes it one command
at a time.

### Storage

```
DAT_0048c5ac                 command count / stack pointer
DAT_0048c74c + i*6 + 0       opcode (u16)
DAT_0048c74c + i*6 + 2       arg1   (u16)
DAT_0048c74c + i*6 + 4       arg2   (u16)
```

Event handlers (`ichicheck` targets, `handan` branches, NPC functions) append
commands to this array.  `stackwork` (`0x4526c0`) executes the current command
and decrements the count.  The game loop/timer calls it repeatedly, so the
list behaves like a timeline.

### Opcodes

| Opcode | Meaning |
|---:|---|
| `1` | play sound/music |
| `2` | set sprite |
| `3` | altar/stat event |
| `4` | signed player-life delta |
| `5` | signed monster-HP delta (cross/Dragon checks) |
| `6` | set map tile |
| `7` | add gold (Lucky Gold multiplier) |
| `8` | show item/key UI |
| `9` | three-sprite animation |
| `10` | start a sub-event |
| `0xb` | sub-dispatch: NPC/boss/title/floor-change/etc. |
| `0xc` | monster movement |
| `0xd` | door animation |
| `0xe` | battle hit animation + monster damage |
| `0xf` | stop BGM |
| `0x10` | add life |
| `0x11` | silent map-tile set |
| `0x13` | halve life |
| `0x14` | set state field |
| `0x15` | draw actor at map index |
| `0x16` | reset/new game/ending |

Sub-dispatch `0xb` includes `openning31`, `zenolivework`, `madoushi2`,
`banpaiawork`, `moncheck`, game-over/ending, title screen, floor change,
UI cleanup, and fades.

### Important limitations

* There are **no expressions, loops, or general branches** in the VM.  The
  branching/conditions live in the event handler code that builds the list.
* State is global (`DAT_004b...`), not script-local.
* Animation/timing is mostly explicit commands and frame waits.
* The VM is a **presentation/action sequencer**, not a programming language.
* Re-implementing it exactly is possible, but the command list is long and
  brittle; translating it to a higher-level event representation is usually
  better for a new engine.

## 2. Option A — TSV action table

A generated table such as:

```
event_id   step   action        arg1        arg2        arg3
floor10    0      sound         5
floor10    1      message       0x123
floor10    2      battle        14
floor10    3      set_tile      60          6
floor10    4      end
```

A small C runner interprets `action` and its arguments.  Branches use
`goto`/`if` actions and step labels.

**Pros**

* No extra dependency; a ~200-line C parser/runner.
* Data is inspectable and diffable; easy to generate from the RE timelines.
* Close to the original command list; low conceptual overhead.
* No code execution/sandboxing concerns.

**Cons**

* You end up inventing a mini-language for conditions, choices, loops, and
  local state.  NPC/boss logic quickly becomes a pile of `goto`/`if` rows.
* Weak type safety and runtime errors; harder to debug than a real script.
* The action vocabulary grows; every new event pattern needs a new action.
* Not pleasant for complex branching or reusable event helpers.

**Best fit:** linear timelines and the first vertical slice, where the event
set is small and we want zero dependencies.

## 3. Option B — embedded Lua

Embed Lua 5.4 (MIT license) and expose a small event API:

```lua
function floor10(ctx)
    ctx:sound(5)
    ctx:message(0x123)
    ctx:battle(14)
    ctx:set_tile(60, 6)
    ctx:wait(10)
end
```

Coroutines can yield for animations, choices, and battles, so event scripts
read naturally.  Static tables (maps, monsters, items, messages) remain TSV.

**Pros**

* Real control flow: `if`, loops, functions, tables, local variables.
* Much better for NPC/boss logic, choices, reusable helpers, and modding.
* Scripts can be edited/hot-reloaded; text/data remain separate.
* Lua is small, portable, MIT-licensed, and easy to embed in C.
* The original command list maps almost 1:1 to API calls; `wait`/animation
  becomes a coroutine yield.

**Cons**

* Adds a dependency and build integration (vendor the Lua source).
* Need a C API layer for messages, battles, map, state, gold, life, floor,
  teleport, sound, sprites, doors, choices, and waits.
* Error handling/sandboxing and a debug console are extra work.
* Slight overkill for the first 10 floors if we only need linear timelines.
* Performance is irrelevant for this game.

**Best fit:** long-term maintainability and complex event logic.

## 4. Recommendation: hybrid

Use **TSV for static tables** (maps, monsters, items, messages) and **Lua for
event scripts**.  This keeps data simple and event logic expressive.  The Lua
API can be tiny at first:

```
message(id)          battle(monster_index)
choice(prompt, ...)  set_tile(index, value)
set_state(index, v)  add_gold(n)
damage(n) / heal(n)  change_floor(n) / teleport(index)
wait(frames)         sound(id) / sprite(id, ...) / door(...)
```

The original timeline VM can be represented as Lua coroutines; if exact
animation parity is needed later, a compatibility layer can emulate the
6-byte commands.

If avoiding dependencies matters more than event maintainability, choose
**TSV-only** for the first slice and keep the runner isolated so it can be
replaced by Lua later.  A generated-C table is a third option, but it has the
same expressiveness limits as TSV plus recompilation on every event change.

## 5. Comparison

| Criterion | TSV action table | Lua + TSV data |
|---|---|---|
| Dependency | none | Lua 5.4 (vendored, MIT) |
| Build complexity | low | medium |
| Expressiveness | low/medium | high |
| Branching/choices | custom mini-language | natural |
| NPC/boss logic | awkward | natural |
| Inspect/diff | excellent | good |
| Debugging | custom runner | Lua traceback + API |
| Modding | limited | strong |
| Runtime safety | no code execution | needs sandbox/limits |
| Fit to original VM | direct | direct via API/coroutines |
| First-slice effort | lower | slightly higher |
| Long-term effort | higher | lower |

**Recommendation:** Lua for events, TSV for data.  If you prefer zero
dependencies for the first slice, TSV-only is a reasonable temporary choice,
provided the event runner is isolated for a later Lua migration.

## 6. First decision: Lua coroutines (superseded)

The first choice was Lua event scripts run as coroutines, with typed wait
requests (ticks, message, animation, choice, battle) on a fixed 60 Hz clock
and a small API (`message`, `choice`, `battle`, `set_tile`, `wait`, …).
Section 7 replaced that execution model; the coroutine runtime and
`events.lua` were removed once no event used them.  What carried over:

* Lua 5.4 is vendored under `third_party/lua` (MIT) and built as a static
  library, so there is no system Lua dependency.
* Lua is used for events only; maps, monsters, items and messages are C
  tables or generated data.
* Scripts are committed source in `src/events/`, installed with the build and
  loaded with `luaL_loadfile` + `lua_pcall` and a traceback handler.
* `io`/`os` are not exposed; the API surface stays small.
* Only one event runs at a time; persistent once-only flags live in the
  serialized `GameState`; save is rejected during an event and load/new game
  cancels it.

### Original cursor note

The original VM decrements a command count.  P1 recovered the cursor order:
`stackwork` executes the command at the current count and then decrements the
count, so the timeline is a tail/LIFO sequence.  `0xb` sub-events decrement the
count, append a replacement sequence, then increment it again; there is no
separate event stack.  See [`../re/12-event-vm.md`](../re/12-event-vm.md).
Section 7 makes this cursor the engine's event execution model.

## 7. Revision: command VM first

### Why the decision changed

* **The analysis recovers command streams, and the coroutine scripts could not
  consume them.**  The pilot (`../re/16-event-pilot.md`) reproduces floor 10 as
  462 commands; the coroutine `10:floor` script was a message, a battle and a
  message, so nothing recovered from the binary could reach the engine.
* **The timing is in the command stream.**  194 of the 312 commands appended by
  the floor-10 handler itself are frame steps (opcode `0x00`), and
  `timer2timer` consumes at most two commands per timer event.  A fixed 60 Hz
  `wait(n)` is an assumption about the original rate, not a recovered fact.
* **Parity needs a common observable.**  The native oracle
  (`../re/14-event-oracle-contract.md`) returns command words and state/map/aux
  effects for a snapshot.  An engine that produces the same words can be
  checked mechanically; an engine that produces "a dialogue, then a battle"
  cannot.

### Execution model

* `src/event_vm.c/.h` — a C port of `docs/re/tools/event_vm.py`: the
  `(opcode, arg1, arg2)` u16 buffer, the count/cursor, tail/LIFO consumption,
  the opcode table and the `0x0b` sub-dispatch from `../re/12-event-vm.md`,
  including the cursor splice for subcommands `1`, `2`, `3`, `5`, `10` and `6`.
* **Builders are Lua functions, one per original handler** (`roujin`, the
  `mevent` floor cases, the `ichicheck` floor handlers, `zenolivework`,
  `doorstackin`, …), named after the original and annotated with its address.
  The name comes from the analysis's two-level model
  (`../re/11-event-analysis-plan.md` §4): the original handler is the
  *builder program* that guards, mutates and appends; the *command timeline*
  it produces is separate.  A builder runs to completion without yielding,
  exactly like the original: it reads state/map/auxiliaries, applies
  builder-side effects, and appends commands.  Minimal API:

  ```
  emit(op, arg1, arg2)        get_state(i) / set_state(i, v)
  get_tile(i) / set_tile(i,v) aux.<name> (message_selector, event_selector,
  call(builder_name)                      target_index, direction, ...)
  ```

  Builders emit in the original append order; the VM consumes from the tail.
* **Splice subcommands call builders at VM time**, so their output depends on
  the state left by earlier commands (for example `0x0b/10` → `taisen2`).
* **Triggers** follow `../re/13-event-triggers.md`: the `handan` raw-tile
  switch, the `mevent`/`ichicheck` floor tables and the `Timer1Timer` checks
  select the builder.  No `(floor, map_index)`-keyed placeholders remain.
* **Presentation opcodes are engine primitives in C**: `0x01` sound/media,
  `0x02` sprite, `0x03`/`0x0a` the `zyouout` window, `0x08` item/key UI,
  `0x09` three-sprite animation, `0x0c` `monidou`, `0x0d` `dooropen`, `0x0e`
  battle hit, `0x15` draw actor, and the direct `0x0b` subcommands (title,
  player draw/animation, floor change, hide UI/images, fade).  A primitive may
  be a logged stub until its presentation is implemented; state-changing
  opcodes may not.
* **Clock.**  The VM is stepped by a `timer2`-equivalent tick: up to two
  commands per tick, with the original intervals (form default, speed menu,
  `dooropen`, `openning9`; `../re/12-event-vm.md` §10).  Rendering runs
  independently; tests step the tick manually.
* **Modal presentation.**  Op `0x0a` stops Timer2 and opens the dialogue
  window; confirming runs the `Button1Click` builder, which ends in
  `timer2on` (`../re/12-event-vm.md` §11).  Choices (merchants, altars) wait
  the same way.  Event battles (`taisen2`) are self-driving timelines, not
  modal.

### What stays

* Lua 5.4 vendoring, loading, traceback handling and the sandbox
  (section 6).
* C tables or generated data for maps, monsters, items and messages.

### Acceptance

For every ported builder, golden vectors exported from the native oracle
(snapshot → command words, state/map deltas, auxiliaries) are compared
byte-for-byte in `ctest`.  Vectors are generated data: they live outside the
repository and the test is skipped when they are absent.  Rendering is still
not compared pixel-for-pixel.

### Migration order

Complete: the VM and its tests, the builder API, `roujin`, the floor-3
opening, floor 10 through `ichicheck`, and the removal of the coroutine
runtime (`events.c` is engine-only).  Further porting follows
`../re/08-implementation-plan.md` (Phase 4).

Dialogues: op `0x0a` and NPC `zyouout` calls open the engine's window with
text from `ListBox2` (`../re/18-messages.md`); Enter runs the
`button1click` builder.  Timer2 is explicit engine state (op `0x0a` and
`openning9` phase 8 stop it, `timer2on` starts it).
