# Event-system analysis plan: trace-free recovery

This plan defines the work needed to turn the current partial event analysis
into a complete specification that can be re-implemented **without observing
gameplay**.  Recorded gameplay may still be used as an optional cross-check,
but it must not be the source of any trigger, condition, or timeline step.
"Trace-free" means *no recorded gameplay*; it does not mean *no execution*:
running the original handlers on synthetic snapshots in the native oracle is a
primary extraction method (see the revision below).

Status: **P0–P3b are complete; P4 is in progress.**

* P0–P2.5: binary pin, buffer-writer inventory, VM semantics
  ([`12-event-vm.md`](12-event-vm.md)), trigger tables
  ([`13-event-triggers.md`](13-event-triggers.md)) and the oracle contract
  ([`14-event-oracle-contract.md`](14-event-oracle-contract.md),
  [`15-event-flags.md`](15-event-flags.md)).
* P3: the static composer reproduces the pilot streams (floor 10, `roujin`,
  `zenolivework`, `openning2`, the floor-3 trigger) with no unresolved
  effects, and the native oracle produces the same streams.  The composer is
  frozen at that scope (section 0).
* P3b: `run_execution_oracle.py` drains the original `stackwork` after the
  builder, suspends on opcode `0x0a` and confirms dialogues through the
  original `Button1Click`.  Its findings are in
  [`16-event-pilot.md`](16-event-pilot.md) §8.
* P4: `check_event_coverage.py` maps ordered oracle runs onto the static CFG
  and is the gate for each ported handler; the current matrix is in
  `16-event-pilot.md` §9.  Section 13 lists what is left.

## 0. Plan revision: oracle-first

P0–P3 stand as recorded.  From here the plan changes in four ways; the
sections below are edited to match.

1. **Emulation is an extractor, not only a validator.**  The native oracle
   already runs the original handlers deterministically, fail-closed and
   without gameplay.  What it returns for a snapshot is evidence of the same
   rank as a statically lifted row.
2. **The static composer is frozen at pilot scope.**
   `compose_event_timeline.py` reaches parity through per-address path, loop
   and effect contracts, and each new witness has needed new contracts.  It
   stays as a regression cross-check for the pilots; no contracts are added
   for further handlers and the "reusable symbolic forms" work is dropped.
3. **The static CFG becomes the coverage map.**  The lifter and the
   whole-binary scanners say which blocks, edges and append sites exist and
   which predicates guard them; the oracle is run until those are covered.
4. **The deliverable is golden vectors plus builder notes**, not a builder-IR
   table with its own expression language.  The engine ports each handler as a
   Lua builder (`../design/01-event-system.md` §7) and is accepted on vector
   parity.

## 1. Objective

Produce a static specification of:

1. every event trigger: floor, map index, raw tile, caller, guard condition,
   and one-shot flag;
2. every event timeline: ordered commands, dynamic arguments, and the state
   predicate under which each command is appended;
3. the builder-side behaviour that mutates state, calls other event functions,
   or changes the map before and during command construction;
4. the state/event flag map used by those conditions;
5. the timeline VM semantics, including opcode `0x12`, opcode `0`,
   sub-event stacking, and command consumption order.

**Done means:** a new engine can decide *what happens* for every event from the
generated data alone.  The original executable is an oracle for validation, not
a prerequisite for understanding an event.

## 2. Baseline and verified evidence

Already reasonably solid:

* timeline storage and runner: count at `0x48c5ac`, 6-byte commands at
  `0x48c74c`, runner `stackwork` at `0x4526c0`;
* `ichicheck` (`0x46cb1c`) floor table at `0x46cb63` and jump table at
  `0x46cb8b` for floors 10..49;
* much of the opcode vocabulary and the `0xb` sub-dispatch;
* several individual triggers: floor-2 prisoner, Zeno on floor 3, floors 5/9
  secret walls, floors 15/35 `Timer1Timer` events;
* map tile ranges, NPC floor tables, special-item slots, and equipment data.

Verified directly against the pinned binary for this plan:

* SHA-256:
  `41cb9844d9bdcf2641a34d4b152d6fbbb0455077abe0e9d7bfed77761a4d2779`;
* `mevent` (`0x46397c`) reads floor `0..49`, indexes a 50-byte table at
  `0x4639b6`, and jumps through a 17-entry pointer table at `0x4639e8`
  (case indices `0..0x10`).  Guards: floor load `0x46399b`, bound `cmp
  eax,0x31` at `0x4639a0`, table load at `0x4639a9`, indirect jump at
  `0x4639af`.  Several case bodies call `ichicheck`;
* `roujin` (`0x449708`) reads floor `0..48`, indexes a 49-byte table at
  `0x449767`, and jumps through a 19-entry pointer table at `0x449798`
  (case indices `0..0x12`).  Guards: floor load `0x44974c`, bound `cmp
  eax,0x30` at `0x449751`, table load at `0x44975a`, indirect jump at
  `0x449760`;
* `roujin` writes the count at `0x48c5ac` and commands directly to
  `0x48c74c`/`0x48c74e`/`0x48c750` (count increment at `0x44983b`, command
  stores from `0x449842` onward), so the current extractor window and
  `inc [ebx]` pattern miss it.

Still open (the rest of the original list is resolved: `timer2` intervals in
`12-event-vm.md` §10, message IDs in `18-messages.md`, altar/choice
presentation in the engine):

* bulk handler coverage: handlers are covered by oracle witnesses, not by
  symbolic lifts (section 0); the current matrix is in `16-event-pilot.md`
  §9.  `extract_timelines.py` remains the window-based legacy inventory;
* `mevent`'s per-case tile/index guards and exact map-index owners, beyond
  the cases with witnesses;
* `handan`'s per-case guards for the raw families without witnesses
  (`0x0a`, `0x1b`/`0x1c`, the event-marker ranges);
* the event-flag region `0x4b8730..0x4b88fb` beyond the flags in
  `15-event-flags.md`.

Already resolved by P1: opcode `0x14`'s state indices are direct
`GameState` indices; opcode `0`/`0x12` are no-ops (`0` is the one-invocation
frame step); sub-events are cursor splices, not a stack; and the
tail/LIFO consumption order is documented in `12-event-vm.md`.

## 3. Gameplay traces

Earlier plans gated the opening event on a live gameplay trace.  That gate is
gone: the floor-3 trigger was resolved statically and by the oracle
(`16-event-pilot.md`).  A trace may still corroborate a result; it is never
the source of one.

## 4. Method and principles

1. **Pin the binary.** Record SHA-256 and version of `TSW.exe`.  Every address
   in generated data refers to that hash.  A different build invalidates the
   tables and must be rejected.
2. **No recorded gameplay; static analysis and emulation together.**  Recover
   structure (tables, call graph, CFG, predicates) statically.  Recover
   behaviour by executing the original code in the native oracle on synthetic
   snapshots chosen from that structure.  Never use a recorded gameplay
   sequence as a source.
3. **Two-level model.**  Separate the handler's *builder program* (guards,
   mutations, calls, loops, appends) from the *command timeline* the handler
   produces.  The current row schema cannot express builder-side effects.
4. **Every claim has an address.**  A trigger or timeline row must cite the
   instruction/table address and the predicate that selects it.
5. **Fail closed.**  Unknown stubs, unmodelled globals, truncated loops, or
   unresolved writes do not count as analysed.
6. **Generated data stays outside the repository.**  Commit the extractors,
   schemas, and refined human-readable notes.  Keep raw disassembly, command
   dumps, and extracted game text in the local cache as required by the
   project asset policy.
7. **Idempotent tools.**  Re-running the pipeline on the pinned binary must
   reproduce the same tables byte-for-byte.

## 5. Workstreams

### WS0 — Binary pin and whole-binary access inventory

Tasks:

* record hash, size, timestamp, and section table of the tested binary;
* build a def-use inventory of every read/write reachable from
  `0x48c5ac` and `0x48c74c`, including direct absolute stores, displaced
  addresses, register-held pointers, and helper functions that write either
  address;
* classify each access as count update, command store, reset, or unrelated
  data access;
* report unresolved candidate accesses explicitly; pattern matching alone is
  not coverage.

Deliverable: `docs/re/tools/scan_event_buffers.py` and a local
`event_writers.tsv`.

Exit criterion: every access to the count/buffer is classified or listed as an
unresolved candidate, with no silent omissions.

### WS1 — Timeline VM specification

Tasks:

* decompile `stackwork` (`0x4526c0`) and the `0xb` dispatch completely;
* enumerate every opcode `0x00..0x16`, including `0x12`;
* define exact argument meaning for each opcode, including signed arguments,
  map-index encoding, state indices, and sprite/sound IDs;
* determine the cursor/count semantics, sub-event stack, and what opcode `0`
  actually waits for;
* identify every external function called by an opcode and classify it as
  state mutation, presentation, or control flow;
* map `0x14` state indices onto the global state array and `GameState`.

Deliverables:

* `docs/re/12-event-vm.md` — opcode/argument/state reference;
* `docs/re/tools/event_vm.py` — reference interpreter for tests;
* VM regression tests covering each opcode, nested events, and wait kinds.

Exit criterion: every opcode has a documented effect and a test; no unknown
bytes in the dispatch table; `0x12` and opcode `0` are resolved.  Full flag
semantics may be completed in WS4; the structural VM mapping must be complete
here.

### WS2 — Trigger graph and entry-point contract

Tasks:

* create Ghidra functions for all method-table event entries (`handan`,
  `mevent`, `ichicheck`, `Timer1Timer`, `timer2timer`, `syounin`, `zyouout`,
  `roujin`, `touzoku`, `madoushi1`, `madoushi2`, `banpaiawork`, `yousei`,
  `openning*`, `stackwork`, and the `0x444xxx` call block);
* decode every switch table used by the event path:
  `handan` raw-tile switch, `mevent` floor table `0x4639b6`/jump table
  `0x4639e8`, `ichicheck` `0x46cb63`/`0x46cb8b`, `roujin` floor table
  `0x449767`/jump table `0x449798`, and any NPC floor tables;
* for each call site, record caller, table index/raw-tile family, destination
  handler, and the caller guard where it is already evident; defer per-case
  guards and full ABI details to P2.5/WS3 when they depend on the builder IR;
* classify every raw tile value and event-marker range (`0x5b..0x5d`,
  `0x61..0x69`, `0x73..0x7b`, `0x85..0x9d`) by owner family; per-floor owner
  resolution remains WS3;
* record the partial native calling convention (Delphi register convention,
  first form pointer in `eax`) and identify the fields P2.5 must recover.

Deliverables:

* `docs/re/13-event-triggers.md` — trigger model, switch tables, call graph;
* `docs/re/tools/scan_event_triggers.py` (decodes switch tables and call sites);
* `event_triggers.tsv` generated locally.

Exit criterion (P2a): every switch table and direct call site is inventoried;
every trigger row has a handler and a coarse condition.  Exact per-case guards,
one-shot flags and full entry-point ABI are P2.5/WS3 deliverables.

### WS3 — Handler builder IR and timeline recovery

The task list below is the method used for the P3 pilot and is complete for
it.  Bulk handlers follow the revised method after the pilot paragraph.

Tasks (pilot):

* reconstruct the CFG of each handler from all incoming jump-table targets;
* normalise append forms into a single `append` operation, including the
  `inc [ebx]` macro and direct absolute stores;
* resolve branch predicates to expressions over floor, map index, raw tile,
  state flags, equipment, keys, gold, and map contents;
* lift each basic block into builder IR operations (see section 6), preserving
  the order of guards, mutations, calls, and appends;
* model external calls as typed effects or opaque stubs with documented
  side effects; any stateful call must have executable semantics before a
  handler is declared complete;
* handle loops and dynamic arguments; flag data-dependent loops for symbolic
  or bounded analysis with an explicit bound justification;
* produce one builder program per handler as the canonical artefact; resolved
  command timelines are per-input validation artefacts, not a replacement for
  the builder IR;
* attach a witness/input identifier to every resolved timeline so that inputs
  with the same branch path but different literal arguments remain distinct.

Pilot before bulk work: recover three representative handlers end-to-end —
floor 10 (`0x46cbc7`), `roujin` (`0x449708`), and the floor-3 opening path.
These cover a long boss sequence, an NPC with a floor switch and direct buffer
stores, and the unresolved cutscene trigger.  Record pilot-relevant flags and
IDs during the pilot rather than deferring them.

The floor-10 pilot must enter through `ichicheck` (`0x46cb1c`) with floor 10
selected, not by calling the interior target `0x46cbc7` directly.  If an
interior block entry is ever used, recover its live-in registers, stack
context, and exit continuation explicitly and prove that it matches the
enclosing function's entry state.

The opening pilot covers the floor-3 `(4,8)` map trigger, now resolved as
`floor==3 and raw==0x07 -> handan 0x4447d2 -> mevent case 0x463a2c`; the case
clears the tile `0x07 -> 0x06` (one-shot) and calls `zenolivework`.  The
separate new-game launch path enters `Button1Click` → `openning2`; the
one-time `open0.bmp` boundary is implemented in `src/intro.c` (document 16
§10).  `ichicheck` is not part of the floor-3 map trigger.

Bulk method (revised).  After the pilot, handlers are not lifted into
symbolic builder programs.  For each handler:

1. lift its CFG and append sites with `lift_event_handler.py` (structure only);
2. derive witness snapshots from the branch predicates (flag values, floor,
   x/y, tile, equipment, facing) and from the boundary values of comparisons;
3. run each witness through the native oracle and record the executed blocks;
4. repeat until every block and edge of the handler is executed, or the
   remaining ones are shown unreachable with the predicate that excludes them;
5. export one golden vector per witness (section 9) and write the builder
   notes the engine port needs (guards, builder-side effects, callees).

Deliverables:

* `docs/re/16-event-pilot.md` — pilot CFG, contracts and witness matrix;
* `docs/re/tools/lift_event_handler.py`, `build_event_ir.py`,
  `compose_event_timeline.py` (pilot scope only);
* per-handler builder notes for the bulk handlers (one document per family);
* golden vectors generated locally.

Exit criterion: every append site in the binary is assigned to a handler and
a basic block; every block is executed by at least one witness or has an
unreachability argument; the three pilot handlers are fully reproduced.

### WS4 — Event flags and persistent state

Tasks:

* statically inventory reads/writes to `0x4b8730..0x4b88fb`;
* add a dynamic def-use inventory: memory read/write hooks on the state block
  during oracle runs, recording the instruction address of every access.
  Static scans miss computed writes (opcode `0x14`, indexed stores such as
  `state[DAT_0048c55c]`); the unresolved floor-10 `0x6a` setter is the first
  target;
* assign each index a semantic name and legal values;
* record which handler sets it, which handlers read it, and whether it is
  one-shot, a counter, or a stage selector;
* reconcile with `syokidata` reset behaviour and the save block layout;
* use save diffs only as a secondary validation of the static mapping;
* map the `0x14` opcode indices onto the same table.

Deliverables:

* `docs/re/15-event-flags.md`;
* `event_flags.tsv` generated locally.

Exit criterion: every flag referenced by an event handler or `0x14` has a name,
range, and producer/consumer list; the one-shot conditions are explicit.

### WS5 — Supporting IDs and presentation data

Tasks:

* build a verified message mapping keyed by *(message list/source, runtime ID)*,
  not by runtime ID alone; include the DFM component/list identity and ordinal;
* map sprite, actor, sound, cutscene-image, and animation-frame IDs;
* record actor spawn slots, positions, facing, movement paths, and removal
  conditions from the handler code;
* map title/cutscene/fade waits to VM commands and wait kinds.

Ordering (revised): the message mapping is not deferred to the end.  Each
handler's messages are mapped when its vectors are exported, starting with the
pilot events, because the engine cannot replace placeholder text without it.

Exit criterion: every timeline argument that is not a plain integer state value
resolves to a named data item in the correct namespace.

### WS6 — Oracle harness and differential validation

Tasks:

* build three oracle modes (see section 7): a **builder oracle** that invokes one
  recovered handler and captures the command buffer and immediate state/map
  effects, a **VM execution oracle** that runs `stackwork` with a controlled
  clock and input/callback schedule, and a **trigger oracle** rooted at the
  real dispatch entry points;
* recover the original timer intervals, speed settings, and callback ordering
  and map them to the synthetic clock before claiming wait parity.  Start with
  the cheapest evidence: the `Interval` properties of the timer components in
  the form data, then any runtime setter;
* build the **execution oracle early (P3b)**: after a builder returns, keep
  calling `stackwork` until the count drains, with scripted completions for
  dialogue/choice/battle and memory hooks on state, map and auxiliaries.  Log
  the auxiliary values each command sees when it is consumed.  This is what
  explains builder-side values that differ at consumption time (the `handan`
  `event_selector`/`event_sprite` exclusion) and how modal presentation
  suspends consumption;
* make trigger cases physically possible: the player stands on a tile
  adjacent to the target, and the case enters through the input handler.  The
  floor-3 pilot currently starts on the target tile itself;
* add trigger-rooted oracle cases that start at the real dispatch entry points
  (`formkeydown`/`timer3ontimer` → `handan`, `Timer1Timer`, `mevent`, arrival,
  post-battle, new game/load) and compare the selected event or no-event
  outcome, player movement, and immediate state changes;
* initialise the serialized state, map, and auxiliary globals (target index,
  direction, UI/mode flags, form objects, string-list pointers) from
  deterministic snapshots;
* classify every external call as modelled, presentation-only with a typed
  stub, or unsupported; fail closed on unsupported calls or inputs;
* intercept filesystem operations so checkpoint/ranking handlers (`yousei`)
  cannot touch the installation's save/ranking files;
* compare the original oracle, the reference interpreter, and the
  reimplementation on the same inputs, including the normalized presentation
  observable log;
* run static closure checks: all buffer writers covered, all switch cases
  decoded, all append sites accounted for, no unknown opcode, no unresolved
  flag index, no truncated loop counted as complete.

Deliverables:

* `docs/re/tools/native_event_oracle.py`, `run_native_oracle.py` (exist);
* `docs/re/tools/run_execution_oracle.py` — execution-oracle mode (builder +
  drain + hooks; exists);
* `docs/re/tools/export_event_vectors.py` — witness snapshots in, golden
  vectors out;
* `docs/re/tools/check_event_coverage.py` — executed blocks/edges/append
  sites against the static inventory (implemented; `--require` gate);
* a trace-free CI target (requires the user's own binary; skipped otherwise).

Exit criterion: the oracle and generated timelines agree for the coverage
matrix; the coverage checker reports full coverage for the pinned binary; no
event in the reimplementation depends on a recorded trace.

## 6. Builder IR: the missing representation

Scope note: the builder IR is the pilot's analysis representation.  It is not
extended into a general executable format and it is not the engine's input;
see section 0.

A timeline row is only an *output* of the handler.  Handlers also mutate state,
write map tiles, call other event functions, and choose commands conditionally.
The analysis therefore stores two layers:

**Layer 1 — `event_builder_ir.tsv`**: an executable description of the handler.

```text
handler_id  path_id  step  kind  target  expr  condition  source_addr  notes
```

Allowed `kind` values:

| kind | Meaning |
|---|---|
| `guard` | predicate that must hold for the path; no side effect |
| `set` | write a state/global/map field |
| `call` | typed call with effect signature |
| `append` | append `(opcode, arg1, arg2)` to the command buffer |
| `loop` | bounded or symbolic loop with an explicit bound source |
| `goto` | explicit control-flow edge |
| `stop` | handler return/end |

Predicates and dynamic arguments use a small, documented expression language.
Every expression is evaluated **during list construction**, against the builder
state at that step.  `append` rows record the resulting literal `u16` arguments;
if an argument depends on live VM state at execution time, that is a documented
opcode property, not a builder expression.

**Layer 2 — `event_timelines.tsv`**: resolved command rows for one concrete
input witness.  These rows are validation/generation artefacts derived from the
builder IR; they are not the canonical source when arguments depend on input
state.  Two inputs can follow the same branches and still produce different
literal arguments, so each timeline carries an input/witness identifier.

```text
timeline_id  input_id  path_id  step  label  opcode  arg1  arg2  source_addr  notes
```

The builder IR must be able to represent immediate effects that append no
command.  A handler is not complete until every `call` has executable semantics
and every branch has a guard or an infeasibility result.

## 7. Oracle harness contract

The native oracle must not be a simple `call handler(state)` shortcut.  Define
the contract before the pilot:

* **Entry contract:** distinguish a true function entry from an interior
  jump-table/basic-block entry.  For a function entry, recover the calling
  convention, argument/object registers, stack context, and return
  continuation from its call sites.  For an interior entry, recover live-in
  registers, stack context, and the exit continuation, or enter through the
  enclosing function instead.  Record which form object or global block must
  be initialised and the source of every required field.
* **Snapshot contract:** one deterministic snapshot contains the serialized
  `GameState`, the 51-floor map block, and all auxiliary globals used by the
  trigger/event path (floor, x/y, target index, direction, UI mode, message
  pointers, RNG state if any).  Unknown inputs are errors, not defaults.
* **Stub contract:** external calls are classified as modelled effect,
  presentation-only typed stub, or unsupported.  Unsupported calls abort the
  case.  No stub may silently return zero unless that is its documented effect.
* **I/O contract:** intercept file operations for saves, checkpoints, and
  rankings; the oracle must never modify the user's installation.
* **Determinism contract:** no real UI, audio, timers, or wall-clock input.
  The VM execution oracle uses a controlled synthetic clock plus scripted
  input/battle/choice completions.  Recover the original timer intervals,
  speed settings, and callback ordering from the code before claiming
  wait-duration parity; the reimplementation's fixed tick rate is not evidence
  of the original rate.
* **Comparison contract:** compare command count and bytes, builder-side
  state/map mutations, selected trigger/no-trigger outcome, player movement,
  VM intermediate/terminal effects, and a normalized presentation-observable
  log.  The log records typed stub invocations in order, including message
  namespace/ID, actor/image/sound arguments, and virtual time where relevant;
  it is compared without requiring pixel or audio matching.  A buffer match
  alone is not enough for VM semantics.

**Mode A — builder oracle:** invoke a handler through its true function entry
(or through a recovered interior-block context) with synthetic inputs, capture
the count/buffer and immediate effects.

**Mode B — VM execution oracle:** invoke original `stackwork` and its
callbacks with a controlled schedule, capture command consumption order,
nested-event behaviour, wait completion, and intermediate/terminal state.

**Mode C — trigger oracle:** invoke the real dispatch entry point (keyboard,
timer, arrival, post-battle, new game/load) and compare which event, if any, is
selected, plus player movement and immediate state changes.

## 8. Coverage and acceptance model

Track coverage separately; do not collapse it into "append sites seen":

| Coverage class | Requirement |
|---|---|
| Buffer-writer coverage | every def-use access to count/buffer classified; exclusions only for accesses proven unrelated, with the evidence recorded as a completed classification; unresolved candidates block completion |
| CFG coverage | every reachable basic block and edge exercised, or proven infeasible with a witness/argument |
| Path coverage | every extracted path has a witness state or an infeasibility result |
| Trigger coverage | every `(floor, map index, raw tile, entry point)` outcome, including "no event" |
| Predicate coverage | boundary values derived from constants, flag ranges, and comparison operators |
| Opcode coverage | every opcode and argument class, including waits and nested events |
| Call coverage | every stateful call modelled; every presentation call stubbed with a typed contract |

Representative grids supplement path witnesses; they do not replace them.
A handler with an unsupported call, an unresolved write, or a truncated loop
cannot be marked complete.

Gating classes (revised).  Four classes block completion and are measured
under emulation: buffer-writer coverage, CFG coverage (executed blocks and
edges), trigger coverage and opcode coverage.  Path and predicate coverage
guide the choice of witnesses and are reported, but do not gate.  The
buffer-writer inventory is not closed by "0 unresolved candidates" alone: the
accesses tagged `+fallback` (`12-event-vm.md` §8) count as covered only once
an oracle run has executed them inside an identified handler.

## 9. Suggested data model

`event_triggers.tsv`:

```text
trigger_id  floor  map_index  raw_tile  entry_point  caller_addr  handler  handler_label  condition  one_shot_flag  notes
```

`event_builder_ir.tsv`:

```text
handler_id  path_id  step  kind  target  expr  condition  source_addr  notes
```

`event_timelines.tsv`:

```text
timeline_id  input_id  path_id  step  label  opcode  arg1  arg2  source_addr  notes
```

`event_flags.tsv`:

```text
index  address  name  values  set_by  read_by  reset_by  notes
```

`event_messages.tsv`:

```text
list_id  runtime_id  dfm_ordinal  text_key  notes
```

Use `*` for a floor-wide trigger and `-` for a not-applicable field.  Keep the
schema stable; the generator and coverage checker both consume it.

Golden vector (one JSON object per witness; generated locally, never
committed):

```text
binary_sha256   pinned hash
case_id         handler + witness name
entry           native entry address
snapshot        state[171], map (base64, 6273 bytes), aux{}
builder         command_words[[op,a1,a2]...], state_diff, map_diff, aux_final
drain           (execution oracle) consumed order, state/map after drain,
                presentation log
blocks          executed instruction addresses and ordered contiguous runs
                (`executed`, `executed_runs`) for coverage
```

`event_builder_ir.tsv` and `event_timelines.tsv` remain analysis-side
artefacts for the pilots.  The engine consumes golden vectors and ported Lua
builders, not those tables.

## 10. Phase order

| Phase | Work | Exit criterion |
|---|---|---|
| P0 | Pin binary; add schemas; build whole-binary writer inventory | Every buffer access classified or reported unresolved |
| P1 | WS1: VM structural semantics; reference interpreter | All opcodes, including `0x12`, documented and tested |
| P2a | WS2: switch tables, call sites, coarse trigger inventory | Every table/call site inventoried; every row has a handler |
| P2.5 | Entry-point ABI; snapshot/stub/filesystem contract; pilot flags/IDs | Contract and pilot flags recorded; native harness remains P3/P4 |
| P3 | WS3 pilot: floor 10, `roujin`, opening path | Three builder programs and timelines reproduced; native oracle runs |
| P3b | Execution oracle (builder + drain + hooks); timer intervals from form data; realistic trigger cases; pilot message IDs | Floor-10 `0x6a` setter found; `handan` selector exclusion explained; floor-3 movement/teleport recovered; intervals recorded |
| P4 | WS3 bulk, oracle-driven: all `ichicheck`, `mevent`, `handan` and NPC handlers | Every append site executed by a witness or shown unreachable; one golden vector per witness |
| P5 | WS4/WS5 completion: flag map from static + dynamic def-use; remaining IDs and presentation data | Every referenced flag and ID resolved |
| P6 | WS6 closure: coverage checker green; engine builders pass vector parity | Trace-free comparison passes for every handler |
| P7 | Documentation and handoff | Notes, schema, and regeneration steps complete |

Do not start P4 before the P3 pilot proves the lifter and oracle on all three
event shapes and P3b has produced a working execution oracle.  P2.5 specifies the contract; P3 builds and validates
the minimal native oracle.  P6 scales it to
full differential and VM-execution coverage.

**P3 acceptance scope (explicit):** P3 is accepted on the bounded witness
matrix in [`16-event-pilot.md`](16-event-pilot.md), not on exhaustive branch
coverage of the three pilots.  The matrix covers the armed/guard-disabled and
nonzero-sound floor-10 paths, `roujin` floor 2 flag clear/set and repeat/selector
paths plus floors 3/4/6 and floor-16 x=9 gate clear/set/off, `zenolivework`,
the width-32/facing-4 `openning2` path, and the floor-3 raw-`0x07`/cleared
`0x06` trigger.  Remaining branch variants are P4 witness-expansion work unless
they reveal a new event shape.  The `open0.bmp` launch intro is now
implemented (document 16 §10); `openning31` remains separate launch follow-up.

## 11. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Handlers interleave Delphi/UI calls with event logic | Classify calls by address; stub presentation calls; require executable semantics for stateful calls |
| Shared tails and duplicated append macros | Normalise both append forms, then group blocks by target handler |
| Indirect jumps and switch tables | Decode byte/pointer tables explicitly; assert bounds and targets |
| Dynamic arguments from map/state | Use builder expressions plus symbolic witnesses; record evaluation timing |
| Data-dependent loops | Detect back edges with dynamic bounds; require a bound proof or mark incomplete |
| Huge state space | Use predicate-derived witnesses and boundary grids, not exhaustive Cartesian products |
| Oracle calls uninitialised VCL objects | Recover the entry contract and snapshot auxiliary globals; fail closed on unknown fields |
| Oracle mutates user files | Intercept filesystem calls; use an isolated temporary save directory |
| Buffer match without VM match | Add the VM execution oracle; compare intermediate and terminal effects |
| Binary variants | Pin SHA-256; fail closed on mismatch |
| Copyright/asset policy | Keep raw dumps and extracted text outside the repo; commit only refined specs and tools |

## 12. Definition of done

* Every writer and reader of `0x48c5ac`/`0x48c74c` is classified; unresolved
  candidates are zero.
* Every append site is assigned to a handler, builder block, and path.
* Every handler has golden vectors covering its blocks, and builder notes
  sufficient to port it; every stateful call it makes is executed natively or
  has a documented contract.
* Every event trigger has floor/index/raw-tile inputs, an entry point, and a
  guard; "no event" outcomes are covered.
* Every event flag has a name, range, producer, and consumer.
* The VM is fully documented, including `0x12`, opcode `0`, sub-events, and
  cursor semantics, and validated by the execution oracle.
* Runtime message/sprite/actor IDs are resolved with namespaces.
* Every path has a witness or an infeasibility result; every resolved
  timeline has an input/witness identifier; no truncated loop or unresolved
  stub is counted as complete.
* The reimplementation's builders and command VM agree with the golden
  vectors (builder and drain parts) on the coverage matrix, including the
  normalized presentation-observable log.
* The reimplementation can run every event with no recorded gameplay trace.

## 13. Next steps

Done so far: typed contracts and untainted drains in the golden vectors; the
message mapping (`18-messages.md`); choice and battle (`taisen2`)
suspension; `Button1Click` case 20; `syounin`, altars, `touzoku`, `yousei`;
all fourteen `ichicheck` floors (69/69 blocks, 1111/1111 append sites);
`moncheck` floors 2, 8, 10 and 40; the `open0.bmp` launch path
(`17-static-findings.md` §3); the coverage checker; and the buffer-writer
re-scan (2,862 command-opcode writes, 0 unresolved; `09-open-questions.md`
item 24).

Open, in order (the engine-side list is in `08-implementation-plan.md`,
Phase 4):

1. Witnesses and drains for the splices that ported floors queue:
   `openning9`.  The losing `taisen2` branch drains (death window, try-again
   `MessageDlg`, op `0x16` → `gameover`; `16-event-pilot.md` §11).  `madoushi2` is done
   (128/128 append sites); `banpaiawork` is presentation only and floor 20
   drains end to end.
2. The `moncheck` matrix: floors 11, 14, 15, 17, 20, 25, 30, 32, 33, 34, 35,
   38, 41, 44, 45, 48, 49 and 50.
3. `idou`'s movement attacks: `mazyutu1` and `madoushi1`.
4. The remaining `handan`/`mevent` cases (raw `0x0a`, `0x1b`/`0x1c`, the
   `mevent` floor-23 case, the marker ranges) and `Timer1Timer`.
5. P5: the flag map from static and dynamic def-use (WS4), remaining IDs
   (WS5); then P6 closure with the coverage checker green for every handler.
