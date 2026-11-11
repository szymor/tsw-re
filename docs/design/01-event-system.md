# Event system design: original VM, TSV, and Lua

This document explains what the original event “VM” actually is and compares
the two practical ways to re-implement it: a TSV action table or embedded Lua.
**Decision: Lua event scripts + TSV static data.**

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

## 6. Decision and implementation

**Chosen: Lua event scripts + TSV static data.**

* Vendor **Lua 5.4** source under `third_party/lua` (MIT; include its license).
  Build it as a static library and link it into the game.  This avoids a
  system Lua dependency and keeps ports self-contained.
* Lua is used for **events/NPCs/bosses** only.  Maps, monsters, items and
  messages stay in TSV/C tables.
* Event scripts live in `src/events/` (committed source, not converted
  assets).  They are installed into the build and loaded at runtime.
* `events.c` owns the Lua state, registers the API, loads the scripts, and
  runs one event as a coroutine.  `wait` yields to the frame loop.
* Initial API: `message`, `choice`, `battle`, `set_tile`, `set_state`,
  `add_gold`, `damage`, `heal`, `change_floor`, `teleport`, `wait`, `sound`,
  `sprite`, `door`.
* Do not expose `io`/`os` by default; provide a controlled debug print.  The
  scripts are ours, but the sandbox keeps the API surface small.
* Error handling: `lua_pcall` with traceback; a script error is logged and
  the event is aborted safely.
* Test plan: a trivial event script in Phase 0, then floor-10/fairy and
  NPC events in Phase 3.

If Lua embedding proves problematic, the event runner is isolated so the
TSV-only path remains a fallback.
