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

## 5. Open items

* Find the setter of `0x4b8830` (floor 10).  Ghidra shows only the
  `syokidata` zero-write at `0x455feb` and the floor-10 read; the path that
  turns it to 1 in later saves is still open.
* The `roujin` selector `0x1b` / state `0x6d` gate is now mapped to the
  floor-16 old-man case at x=9; its dialogue/effect text still needs WS3.
* Recover the actual one-shot flags for floors 4 and 6, if any.
* **Reconcile `0x42..0x45` as state indices.**  Section 3 says these are
  selector values, not state indices.  The pilot composer nevertheless models
  `state[DAT_0048c55c]` as a dialogue stage: its contracts at `0x449c8a`
  (branch on the value 0/1/2), `0x449cbb` (store 1) and `0x449ccd` (store 2)
  index the state block with the event selector, and the floor-3 repeat
  witness (`state[0x43] = 1`) passes native parity.  If the code at those
  addresses confirms it, the selectors double as state indices holding a
  0 → 1 → 2 stage, section 3 needs rewording, and `src/events/events.lua`'s
  use of `0x43..0x45` as flags is closer to the original than the note there
  says (but two-valued instead of three).
* Use dynamic def-use (memory hooks during oracle runs) alongside the static
  scan for the flag map; static xrefs cannot see computed writes.
* Full event-flag region map (`0x2a..0x9c`) and one-shot/counter semantics.
