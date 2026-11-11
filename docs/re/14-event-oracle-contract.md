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
every tracked call.  `edx`/`ecx` are often leftovers from the caller
(e.g. `edx` holding the map base in `handan`), so they are **not** part of the
event-entry contract for these calls.  `Timer1Timer`'s exact `Self`/`Sender`
separation is still open.

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
* the 6273-byte map block (header + 51 floor records with 2-byte trailers);
* auxiliary globals:

| Name | Address | Default |
|---|---|---:|
| target index | `0x48c584` | 0 |
| direction | `0x48c590` | 4 (up) |
| sprite | `0x48c58c` | 0 |
| UI mode | `0x48c5ce` | 0 |
| message selector | `0x48c5a0` | 0 |
| event selector | `0x48c55c` | 0 |
| current monster | `0x48c5b8` | 0 |
| monster HP | `0x48c5bc` | 0 |
| battle flag | `0x4b86b8` | 0 |
| sound state | `0x4b87f0` | 0 |
| facing | `0x4b87e8` | 4 |

* serialized player x/y and floor are read from the state block;
* the selected floor tiles are part of the manifest.

The snapshot JSON is the input of record.  Loading an original save is only a
convenience; the oracle must not depend on the save file path or on live
gameplay.

## 4. Form/VCL stub contract

The handlers read presentation objects from the main form.  The oracle must
provide typed stubs, not a zero-filled form:

| Field | Offset | Role |
|---|---:|---|
| map display | `0x95` | `+0x2c` tile width, `+0x30` tile height |
| map image | `0x6c` | tile drawing |
| message panel | `0x6d` | message/image UI |
| image panel | `0x6e` | title/cutscene images |
| media player | `0x111` | string/media resource access |
| media aux | `0x11b` | sound/title helper |
| sound slots | `0xb5`, `0xb6`, `0x118..0x11a` | BGM/WAV dispatch |
| message image | `0x106` | dialogue image |

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

* Enter through the recovered `mevent`/`ichicheck`/`openning*` path.
* Exact initial predicate and one-shot flag are still deliverables; do not
  assume the provisional open/teleport behaviour.
* Include the separate launch `open0.bmp` sequence as a distinct path.

## 8. Open items

* `Timer1Timer` `Self` vs `Sender` roles and required fields.
* Complete form-field inventory for each entry; the current list is the
  minimum seen in `handan`/`stackwork`/NPC handlers.
* Original timer interval/wall-clock mapping.
* Exact floor-3 opening entry and one-shot flag.
* Full entry-point stack/return context for interior handler blocks.
