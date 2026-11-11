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
* **Writer:** not yet located as a direct store in the floor-10 window; likely
  an opcode `0x14` command or the fairy/checkpoint path.  The guard itself is
  confirmed.
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

uses state index `0x6d` (`0x4b883c`) when the selector is `0x1b`.  Its floor
and dialogue case are not yet mapped; do not equate it with the floor-3 old
man without the WS3 branch lift.

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

* Locate the writer of `0x4b8830` (floor 10).  The read guard is confirmed.
* Map the `roujin` selector `0x1b` and state index `0x6d` to the correct floor
  and dialogue case.
* Recover the actual one-shot flags for floors 4 and 6, if any.
* Full event-flag region map (`0x2a..0x9c`) and one-shot/counter semantics.
