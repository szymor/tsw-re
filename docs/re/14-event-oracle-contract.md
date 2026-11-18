# Event oracle contract

How the original event code is invoked on synthetic state, so that what it
does can be recorded and compared with the remake without recorded
gameplay (`16-event-pilot.md`, Method).  Drains run strict with the typed
contracts of section 4.

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
| shop price | `0x48c594` | 0 |
| shop flag (state index) | `0x48c598` | 0 |
| shop talk flag (state index) | `0x48c59c` | 0 |
| choice mode (word: 0 merchant, 1 altar) | `0x48c5cc` | 0 |
| `g_battle_turns` (syounin: thank-you text + 1) | `0x48c56c` | 0 |
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

Typed contracts added for the execution oracle (`native_event_oracle.py`
`STUB_CONTRACTS`; all register convention, Ghidra-checked argument counts):

| Address | Name | Effect class | Notes |
|---|---|---|---|
| `0x403658` | `System_LStrCat` | runtime string | was executed natively; its realloc (`0x403924`) stopped `openning31` |
| `0x40369c` | `System_LStrCat3` | runtime string | sound/media paths |
| `0x402c34` | `TObject_Free` | runtime | |
| `0x41d718` | `TObject_Create` | runtime | returns the writable scratch object |
| `0x41dad8` | `TBitmap_GetCanvas` | presentation | returns the scratch object |
| `0x42c150` | `TImage_GetCanvas` | presentation | returns the scratch object |
| `0x417e44` | `TCanvas_draw_5args` | presentation | 5 args: pops 8 stack bytes |
| `0x41a530` | `TCanvas_CopyRect` | presentation | pops 4 stack bytes |
| `0x413178`, `0x4131bc` | client width/height | presentation | return 0 |
| `0x430f20`, `0x431188`, `0x431250` | `TMediaPlayer` open/stop/play | media | |
| `0x431c8c`, `0x431c5c` | `TMediaPlayer` SetDeviceType / SetNotify (`soundplay`) | media | byte stores into the player object |
| `0x4135a4` | `TControl.GetText` | presentation | destination string left empty (`zyouout` arg1 5) |
| `0x42c464` | `TTimer_SetInterval` | timing | the interval is in `edx` of the recorded invocation |
| `0x4136ac`, `0x41c160`, `0x4291c8` | small VCL setters | presentation | |

Vtable slots `0x34` (`LoadFromFile`) and `0x38` (`TStrings.Clear`) are
presentation returns.  The scratch object (`0x10001800`) carries the stub
vtable, so virtual calls on returned objects reach typed slots.  Game code
that only looked presentational runs natively instead: `mhyouji`
(`0x442c38`, clears `state[0xa6]`), `yusyahyouji` (`0x443648`, player draw)
and `dooropen` (`0x4770e0`, writes the door tile).

## 5. Filesystem and determinism

* Intercept save/checkpoint/ranking writes.  `yousei` must not touch the
  installation's `save0.dat`, `option*.dat` or ranking files; use an isolated
  temporary save directory.
* No real UI, audio, timers or wall-clock input.
* The VM oracle uses a synthetic clock.  `timer2timer` executes at most two
  `stackwork` steps per timer event; the intervals are in `12-event-vm.md`
  §10.
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

## 7. The native harness

`native_event_oracle.py` maps the PE, a synthetic stack, the FS/null page
and form stubs into Unicorn 2.1 and calls an original function on a
snapshot.  `run_execution_oracle.py` adds the timeline drain
(`16-event-pilot.md` §8) and `export_event_vectors.py` writes what both
return as golden vectors (`tests/vectors`).

Guardrails:

* the pinned SHA-256 is checked before mapping (`--allow-other` is for
  diagnosis only);
* a harness instance is single-use, so no state leaks between cases;
* strict mode fails closed on unsupported calls and transfers (`ok=false`,
  `tainted=true` and the reason); unknown direct, indirect, prefixed and
  jump-table transfers are all screened;
* a run captures the state, the 6273-byte map, the command count and words
  (at most `CAPTURE_LIMIT`, as many as the remake's VM holds; more is a
  failure), the auxiliaries, every intercepted helper call and the binary
  digest.

Modes:

* `strict`: only event logic runs; any presentation helper fails the run;
* `presentation`: also the typed `STUB_CONTRACTS` helpers (name, register
  ABI, argument count, return value, declared effects) and the vetted
  vtable slots of the dummy form objects;
* `permissive`: exploratory only; every run is tainted.

What the helpers do beyond returning:

* a few Delphi runtime helpers run for real (memory copy, allocator,
  structure copy); `GetMem`, `LStrCat`/`LStrCat3`/`LStrCatN` and
  `IntToStr` build real strings, and `ListBox2.Items.Get` (form `+0x44c`,
  `+0x100`, slot `0xc`) returns `"{index}"`, so the status text a handler
  builds can be compared (`16-event-pilot.md`, status-bar parity);
* `disp` records the status bar, `ImageList.Draw` what `mhyouji` draws,
  `BlockWrite` the bytes written; `BlockRead` reads the case's file bytes
  in order and `Reset` rewinds;
* `MessageDlg` returns the case's answer, `ShowModal` returns at once.

The harness is not a VCL emulator: a helper outside the contracts fails
the run until it is typed.  `run_synthetic_oracle.py` and
`compose_event_timeline.py`, the static walkers of the pilot, remain as
regression checks for the three pilot handlers.
