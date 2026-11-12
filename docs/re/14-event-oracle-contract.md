# Event oracle contract (P2.5)

Status: **P2.5 contract draft**.  This document defines how the original event
code is invoked with synthetic state so the builder/trigger/VM oracles can be
compared without recorded gameplay traces.

Pinned binary:

```
sha256 41cb9844d9bdcf2641a34d4b152d6fbbb0455077abe0e9d7bfed77761a4d2779
```

Tools:

* `docs/re/tools/event_snapshot.py` — canonical snapshot input;
* `docs/re/tools/scan_event_abi.py` — call-site register-setup inventory;
* `docs/re/tools/scan_event_triggers.py` — trigger/call graph.

## 1. Oracle modes

| Mode | Purpose | Entry |
|---|---|---|
| Trigger oracle | Which event, if any, is selected; player movement and immediate effects | `formkeydown`/`timer3ontimer`/button → `handan`, or `Timer1Timer` |
| Builder oracle | Capture the command buffer and builder-side state/map effects | true function entry (`ichicheck`, `mevent`, `roujin`, NPCs), or a recovered interior-block context |
| VM execution oracle | Command consumption, waits, nested events and terminal effects | `stackwork` with a controlled clock/callback schedule |

A buffer match alone is not enough: the builder and VM oracles must agree on
immediate state/map changes and on the normalized presentation log.

## 2. Entry contract

All tracked event entries use the Delphi register convention.  The call-site
inventory is in `event_abi_call_sites.tsv`.

| Entry | Address | First argument | Notes |
|---|---:|---|---|
| `formkeydown` | `0x460bd8` | form/self in `eax` | three published parameters; input wrapper only |
| `timer3ontimer` | `0x461760` | form/self in `eax` | mouse-arrow repeat |
| `BitBtn1..4Click` | `0x484b5c..` | form/self in `eax` | on-screen arrows |
| `handan` | `0x444490` | form pointer in `eax` | raw-tile dispatcher |
| `mevent` | `0x46397c` | form pointer in `eax` | floor-indexed map-event helper |
| `ichicheck` | `0x46cb1c` | form pointer in `eax` | floor 10..49 event dispatcher |
| `stackwork` | `0x4526c0` | form pointer in `eax` | timeline VM step |
| `syounin`/`touzoku`/`roujin`/`yousei`/`zyouout` | see method table | form pointer in `eax` | NPC/event helpers |
| `Timer1Timer` | `0x443120` | form/self in `eax`; `Sender` role unresolved | timer secret events |
| `openning31`/`2`/`9`, `zenolivework`, `madoushi2`, `banpaiawork`, `moncheck`, `taisen2` | see method table | form pointer in `eax` | many are not Ghidra functions yet |

The ABI scan shows `eax` is consistently set to the form/self pointer before
every tracked helper call.  It does **not** prove that `edx`/`ecx` are unused:
`edx`/`ecx` are often leftovers from the caller, and the input callbacks
(`formkeydown`) take key/shift arguments separately.  Treat the `edx`/`ecx`
columns as last-write evidence, not as final argument assignments.  Event
helpers below are invoked with only the form pointer confirmed; the exact
keyboard callback argument placement remains open.

### Interior blocks

The `ichicheck` floor handlers (`0x46cbc7..`) are **interior jump-table
targets**, not standalone functions.  They inherit:

* `ebx` = command count (`0x48c5ac`),
* `esi` = command buffer (`0x48c74c`),
* the enclosing `ichicheck` stack/return context.

Do not call `0x46cbc7` as a normal function.  Enter `ichicheck` with floor 10
selected, or explicitly reconstruct the live-ins and exit continuation.

## 3. Snapshot contract

`event_snapshot.Snapshot` is the canonical oracle input:

* 171 signed int32 state values (the 684-byte state block);
* the 6273-byte map block: 2-byte header + 51 × 121 tile bytes + 50 × 2 gap
  bytes; there is no trailer after the final record;
* auxiliary globals:

| Name | Address | Default |
|---|---|---:|
| target index | `0x48c584` | 0 |
| direction | `0x48c590` | 1 (up; 2=left, 3=right, 4=down) |
| sprite | `0x48c58c` | 0 |
| UI mode | `0x48c5ce` | 0 |
| message selector | `0x48c5a0` | 0 |
| event selector | `0x48c55c` | 0 |
| current monster | `0x48c5b8` | 0 |
| monster HP | `0x48c5bc` | 0 |
| render order | `0x489b1f + i` | table |

These named views are derived from the serialized state block, not duplicated
auxiliary globals:

| View | State index | Address |
|---|---:|---:|
| battle flag | `0x0c` | `0x4b86b8` |
| facing | `0x58` | `0x4b87e8` |
| sound state | `0x5a` | `0x4b87f0` |

Facing and movement direction use different mappings: facing `1=down,
2=left, 3=right, 4=up`; movement direction `1=up, 2=left, 3=right, 4=down`.

* serialized player x/y and floor are read from the state block;
* the selected floor tiles are part of the manifest.

The snapshot JSON is the input of record.  Loading an original save is only a
convenience; the oracle must not depend on the save file path or on live
gameplay.

## 4. Form/VCL stub contract

The handlers read presentation objects from the main form.  The oracle must
provide typed stubs, not a zero-filled form:

| Field | Slot (`param_1[index]`) | Byte offset | Role |
|---|---:|---:|---|
| map display | `0x95` | `0x254` | `+0x2c` tile width, `+0x30` tile height |
| map image | `0x6c` | `0x1b0` | tile drawing |
| message panel | `0x6d` | `0x1b4` | message/image UI |
| image panel | `0x6e` | `0x1b8` | title/cutscene images |
| media player | `0x111` | `0x444` | string/media resource access |
| media aux | `0x11b` | `0x46c` | sound/title helper |
| sound slots | `0xb5`, `0xb6`, `0x118..0x11a` | `0x2d4`, `0x2d8`, `0x460..0x468` | BGM/WAV dispatch |
| message image | `0x106` | `0x418` | dialogue image |

Stub policy:

1. **Modelled effect** — state/map/global mutation; must be implemented.
2. **Presentation stub** — records a normalized invocation (kind, message
   namespace/id, sprite/actor/sound arguments, order, virtual time) and returns
   a documented value.
3. **Unsupported** — abort the case.  A stub must never silently return zero
   unless that is its documented effect.

## 5. Filesystem and determinism

* Intercept save/checkpoint/ranking writes.  `yousei` must not touch the
  installation's `save0.dat`, `option*.dat` or ranking files; use an isolated
  temporary save directory.
* No real UI, audio, timers or wall-clock input.
* The VM oracle uses a synthetic clock.  `timer2timer` executes at most two
  `stackwork` steps per timer event; the original wall-clock interval is still
  unmeasured.
* Input/battle/choice completions are scripted events, not real input.

## 6. Comparison contract

Compare all of:

* command count and 6-byte command bytes;
* builder-side state/map/global mutations;
* selected trigger/no-trigger outcome and player movement;
* VM intermediate and terminal effects;
* normalized presentation-observable log;
* snapshot hash and manifest.

A test that only compares the command buffer is insufficient.

## 6b. Synthetic oracle implementation

The first executable oracle is synthetic:

```sh
lift_event_handler.py "$GAME/TSW.exe" --method floor10 --dir /tmp/floor10-ir
build_event_ir.py /tmp/floor10-ir
run_synthetic_oracle.py /tmp/floor10-ir/builder_ir.json snapshot.json \
    --entry-block 0x46cbc7 -o trace.json
```

`build_event_ir.py` merges `appends.tsv`, `blocks.tsv` and `ops.tsv` into
`builder_ir.json`.

`run_synthetic_oracle.py`:

* loads an `event_snapshot.Snapshot`;
* walks the builder CFG from an optional `--entry-block` using simple
  `cmp`/`test` equality predicates over state/aux globals;
* executes only appends in visited blocks, in sequence order;
* applies `write_global` builder effects on the taken path (state and
  `0x48c...` auxiliaries such as `message_selector`/`event_selector`);
* records unknown edges, unresolved switches, missing internal blocks,
  unsupported builder writes and unresolved command arguments instead of
  fabricating values or merging unknown paths;
* executes the resulting command list with `event_vm`;
* records state/map diffs, command words, the presentation log, applied
  builder writes and a snapshot digest;
* can compare two traces with `--compare`.

`complete` in the trace is false if any unresolved argument/effect/edge
remains.  The current pilot traces are: floor 10 complete for the armed
snapshot (276/312 commands), `roujin` incomplete (6/22), `zenolivework`
incomplete (19/36), `openning2` incomplete (2/129), `openning31` incomplete
(0/12).

Limitations: this is not the native original-code oracle.  Complex predicates
and dynamic builder arithmetic (e.g. the floor-2 3% stat boost) remain
unresolved and are recorded rather than guessed.

### Static composer

`compose_event_timeline.py` is the trace-free source of truth for pilot
timelines.  It verifies the pinned executable hash, walks the builder CFG,
applies static path/loop/effect contracts, inlines builder callees at call sites,
and emits the command stream, initial/final state and map, state/map deltas,
effective auxiliaries and resolved effects.  Missing callees, unresolved
switches, missing CFG targets and unresolved arguments are explicit errors.
The pilot static streams now match the native reference exactly: floor 10 462,
`roujin` 6, `zenolivework` 36, `openning2` 651, floor-3 `handan` trigger 41,
with zero unresolved builder effects.  Native traces are retained only as an
independent validation oracle; the differential test compares complete command
words and immediate state/map/aux effects on identical snapshots.

### Native harness prototype

`native_event_oracle.py` maps the PE, a synthetic stack, FS/null page and form
stubs into Unicorn, then calls native event functions.  Guardrails:

* pinned SHA-256 check before mapping (`--allow-other` is diagnostic only);
* single-use harness instance (no cross-case state leakage);
* strict mode fails closed on unsupported calls/transfers (`ok=false`,
  `tainted=true`, explicit termination reason); permissive mode is exploratory
  only and always tainted;
* captures state, the full 6273-byte map, command count/words, truncation flag,
  auxiliary globals, normalized stub invocations and the binary digest;
* truncation makes a run ineligible for success (`ok=false`);
* the runner hashes the effective case inputs (state/map/aux/floor), not the
  source save path, and accepts either a save or a canonical snapshot JSON.

Modes:

* `strict` — only event-logic calls execute; any presentation helper transfer
  is rejected/tainted;
* `presentation` — additionally allows the explicit `STUB_CONTRACTS` helper set
  and the single presentation-return address, and records
  `stub_mode="presentation"`; every intercepted helper records its caller,
  ABI, argument count, return value and declared effects;
* `permissive` — exploratory only; unknown calls/transfers and on-demand data
  pages are stubbed but every such run is tainted and ineligible for validation.

`run_native_oracle.py` runs the pilot cases with `presentation` stubs and emits
JSON traces with commands, effective input manifest (state/map/aux/floor),
initial/final state and map contents, deltas, auxiliary globals, normalized
stub invocations and the effective-input digest.  Canonical snapshot manifests
reject unknown or conflicting auxiliary keys; non-default `render_order_base`
is rejected rather than silently ignored.

Status:

* `syokidata` self-test runs to completion (6,604 steps, 0 stubs);
* native pilot traces complete with `ok=true`, `termination="returned"` and
  `tainted=false`:
  floor 10 (462 commands, 19 invocations), `roujin` floor 2 (6 commands,
  50 invocations), new-game `openning2` (651 commands, 49 invocations), and
  `zenolivework` (36 commands, 28 invocations);
* `roujin`'s native command prefix is exactly
  `(0,0,0),(2,0,0),(0,0,0),(1,2,0),(0,0,0),(0,0,0)`, matching the synthetic
  floor-2 trace;
* `openning31` still terminates at an unclassified Delphi runtime/string path
  (`0x403924`), so the subhandler is not yet native-complete;
* the floor-3 raw-`0x07` trigger now has a native `handan` case: starting on
  `(4,8)` with map index 92 set to `0x07` returns untainted with 41 commands
  and 164 invocations, clears the tile to `0x06`, leaves player x/y unchanged,
  starts with command `(0x0a,0x0a,0)`, and reaches `zenolivework`; the final
  `0x48c55c` selector is `0x67`, so the static `0x14` subevent selector still
  needs a separate trace assertion;
* each intercepted helper has an explicit contract (name, `__register` ABI,
  argument count, return value and declared effects) and is recorded as a
  normalized invocation; unknown direct or indirect calls still fail closed;
* virtual presentation returns are accepted only for the observed vtable slots
  `0x0c/0x1c/0x20/0x28/0x2c/0x80` of the dummy vtable; other slots and direct
  stub-page entries fail closed;
* unknown direct or indirect transfers are screened, including prefixed/indirect
  calls and indirect jump tables; unclassified mapped code is not accepted;
* presentation object globals `0x48c518`/`0x48c51c`/`0x48c520`/`0x48c524` are
  initialized to the vetted dummy object so virtual presentation calls resolve
  to the typed return address rather than null;
* four Delphi runtime helpers (`0x402870` memory copy, `0x403598` allocator,
  `0x403658` string assignment, `0x40923c` structure copy) are executed rather
  than intercepted; their nested transfers pass through the same fail-closed
  screen.  `0x403924` (string/reallocation path) is not yet covered;
* this is a pilot-capable native oracle, not a universal VCL emulator.

## 7. Pilot cases

### Floor 10

* Enter through `ichicheck` (`0x46cb1c`) with floor 10 selected.
* Expected handler `0x46cbc7`.
* Guard flag: state index `0x6a` (`0x4b8830`); zero means the event is armed.
* Compare the 312 append sites, the Golden Knight/fairy sequence and the
  final state/map deltas.

### `roujin`

* Enter `roujin` (`0x449708`) with the pilot floor and form pointer.
* Floor-2 flag: state index `0x62` (`0x4b8810`).
* Floor 3/4/6 flags are not yet confirmed; `0x43..0x45` are also
  `DAT_0048c55c` selector values.
* Compare dialogue selector (`0x48c5a0`/`0x48c55c`), state flag writes,
  attack/defense boost and item-slot writes.

### Floor-3 opening

* Trigger: floor 3, target tile raw `0x07` → `handan` `0x4447d2` → `mevent`
  floor-3 case `0x463a2c`.
* One-shot mechanism: the case clears the tile to `0x06`; no separate flag is
  required for this map action.
* It appends `0x0a`/`0x0a` (dispatched selector `0x14`), then calls
  `zenolivework`.
* `openning2` is the new-game button path, not the floor-3 trigger; include the
  separate launch `open0.bmp` sequence as a distinct path.

## 8. Open items

* `Timer1Timer` `Self` vs `Sender` roles and required fields.
* Complete form-field inventory for each entry; the current list is the
  minimum seen in `handan`/`stackwork`/NPC handlers.
* Original timer interval/wall-clock mapping.
* One-time `open0.bmp` launch path (separate from the floor-3 map trigger).
* Full entry-point stack/return context for interior handler blocks.
