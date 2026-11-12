# Event flags (P2.5 pilot)

Status: **P2.5 pilot flag recovery**.  This document fixes the state-index
formula and the pilot flags used by the floor-10 and `roujin` events.  It also
records a naming error in the earlier notes: `0x42..0x45` are
`DAT_0048c55c` selector values, not necessarily state indices.

Pinned binary:

```
sha256 41cb9844d9bdcf2641a34d4b152d6fbbb0455077abe0e9d7bfed77761a4d2779
```

## 1. Index formula

The state block base is `0x4b8688`:

```
state_index = (absolute_address - 0x4b8688) / 4
absolute     = 0x4b8688 + state_index * 4
```

The event-flag region `0x4b8730..0x4b88fb` corresponds to indices
`0x2a..0x9c`.  Do not use `0x4b8730` as the index base for `GameState`.

Examples:

| Address | State index | Notes |
|---:|---:|---|
| `0x4b8810` | `0x62` | floor-2 old-man flag |
| `0x4b8830` | `0x6a` | floor-10 area-1 guard |
| `0x4b883c` | `0x6d` | `roujin` sub-event `0x1b` gate |
| `0x4b86cc` | `0x11` | special slot 0 (Orb of the Hero) |
| `0x4b8704` | `0x1f` | special slot 14 (Lucky Gold) |

## 2. Confirmed pilot flags

### Floor-2 old man

Code:

```
0x4497e4  DAT_0048c5a0 = 0x19
0x4497ee  DAT_0048c55c = 0x42
0x4497f8  cmp DWORD PTR ds:0x4b8810, 0
0x4497ff  jne 0x449b6a          ; already done
0x449805  mov DWORD PTR ds:0x4b8810, 1
0x44980f  attack += attack*3/100
0x449825  defense += defense*3/100
```

* **State index:** `0x62`
* **Semantics:** one-shot floor-2 old-man completion and the 3% stat boost.
* **Current code:** `src/state.h` and `events.lua` now use `0x62`.

### Floor-10 area-1 event

Code:

```
0x46cbc7  cmp DWORD PTR ds:0x4b8830, 0
0x46cbce  jne 0x46f389          ; event already complete
```

* **State index:** `0x6a`
* **Semantics:** floor-10 area-1 completion/guard.  Zero means the
  Golden Knight/fairy sequence is armed.
* **Writer:** Ghidra xrefs to `0x4b8830` show one write at `0x455feb` inside
  `syokidata` (`0x455cb8`), and it stores **zero**; the other reference is the
  floor-10 read at `0x46cbc7`.  No direct setter has been found.  Later saves
  contain `state[0x6a]=1`, so the flag may be set through a computed state
  write or save/checkpoint path; this remains open.
* **Candidate computed writer:** `Button1Click` (the choice window's
  confirm) stores 1 to `dword[0x4b87c8 + id*4]` = `state[0x50 + id]` for
  sub-event ids `0x0b..0x43`; op `0x0A` sets the id to `arg1 + 10`.  The
  floor-10 guard would be set by confirming id `0x1a` (op `0x0A`,
  `arg1 = 0x10`).  Static only — see `17-static-findings.md` §2.
* **Current code:** `src/state.h` and `events.lua` now use `0x6a`.

### `roujin` selector values

`roujin` sets `DAT_0048c55c` to `0x42`, `0x43`, `0x44`, `0x45` for several
dialogue cases:

| Selector | Address set | State flag checked |
|---:|---:|---|
| `0x42` | `0x4497ee` (floor 2) | `0x4b8810` (`0x62`) |
| `0x43` | `0x44992f` (floor 3) | none in that case |
| `0x44` | `0x449945` (floor 4) | none in that case |
| `0x45` | `0x44995e` (floor 6) | none in that case |

The separate gate:

```
0x449b77  cmp DWORD PTR ds:0x48c5a0, 0x1b
0x449b84  cmp DWORD PTR ds:0x4b883c, 0
0x449b8b  jne 0x449c8a
0x449b91  mov DWORD PTR ds:0x4b86dc, 1   ; special slot 4
0x449b9b  mov DWORD PTR ds:0x4b883c, 1
```

uses state index `0x6d` (`0x4b883c`) when the selector is `0x1b`.  The
selector is set by the **floor-16 old-man case** (`0x44996d`) when the player
x-coordinate is `9`; otherwise the same case uses selector `0x06`/event
`0x46`.  The gate sets special slot 4 (`0x4b86dc`).

## 3. Corrections to previous notes

The earlier statement “old-man flags `0x42..0x45`” mixed two namespaces:

* `DAT_0048c55c` = event/dialogue selector values `0x42..0x45`;
* `GameState` indices are `0x62` (floor 2), `0x6a` (floor 10) and `0x6d`
  (`roujin` sub-event `0x1b` gate).

The floor-10 flag is **not** `0x9c`.  The provisional `0x9c` slot was never
the handler guard.

## 4. Code changes

* `src/state.h`: `ST_EVENT_FLAG_OLD_MAN_F2 = 0x62`,
  `ST_EVENT_FLAG_FLOOR10 = 0x6a`; F3/F4/F6 remain provisional.
* `src/events/events.lua`: matching pilot flag updates.
* Tests use the enum, so the numeric change is transparent to the C tests.

## 5. Resolved by the P3b execution oracle

Dynamic def-use (write hooks in `run_execution_oracle.py`) resolved the two
pilot flag questions:

* **`0x6a` (floor 10, `0x4b8830`).**  Written at `0x44da2a` by
  `Button1Click`, which stores `state[0x50 + id] = 1` for every confirmed
  dialogue sub-event with `10 < id < 0x44`.  The fairy (`yousei`,
  `0x478f0c`) appends `0x0a/16` (sub-event 26), and `0x50 + 26 = 0x6a`.  So
  the area-1 flag means "the fairy's dialogue was confirmed", not "the
  Golden Knight was beaten".  No static xref exists because the index is
  computed.
* **`0x42..0x45` are both selectors and state indices.**  `roujin` writes
  `state[event_selector]` at `0x449cc0` (value 1, or 2 when the Orb of
  Wisdom is held, at `0x449ccd`).  Floor 2 therefore writes `0x42` in
  addition to its one-shot `0x62`; floors 3/4/6 write `0x43/0x44/0x45`.
  Section 3's "selector values, not state indices" is superseded.  The Lua
  builder port encodes this and passes golden-vector parity on all `roujin`
  witnesses.

The `0x50 + id` rule also names the flags the opening sets: confirming
sub-events 20, 21, 11, 12 and 13 sets `0x64`, `0x65`, `0x5b`, `0x5c` and
`0x5d` (`16-event-pilot.md` §8).

## 6. Open items

* The `roujin` selector `0x1b` / state `0x6d` gate is mapped to the floor-16
  old-man case at x=9; its dialogue/effect text still needs WS5.
* `state[0xc]` is set to 1 by `openning31` (`0x465626`) and cleared by
  `Button1Click` sub-event 13 (`0x44dc73`): a transient "opening in
  progress" marker; its readers are not mapped yet.
* `yousei` writes `0xe`, `0x55` (score-like value, 415 on the probe
  snapshot), `0x56` and `0x57`; name them with the ranking work.
* Full event-flag region map (`0x2a..0x9c`) and one-shot/counter semantics,
  using the oracle write hooks across the P4 witnesses.
