# Event triggers and call graph

Status: **P2a inventory complete**.  This document records the static switch
tables and trigger/call-site inventory produced from the pinned binary.
Per-case `mevent` guards, event-flag conditions, exact map-index owners and the
full entry-point ABI are P2.5/WS3 work.

```
sha256 41cb9844d9bdcf2641a34d4b152d6fbbb0455077abe0e9d7bfed77761a4d2779
```

The extractor is `docs/re/tools/scan_event_triggers.py`.  It writes local TSVs
for switch entries, call sites and a first-pass trigger inventory:

```sh
python3 docs/re/tools/scan_event_triggers.py "$GAME/TSW.exe" -o /tmp/opencode
```

Generated data stays outside the repository.  The rows below are a trigger
inventory, not yet final executable per-floor timelines; WS3 still has to
attach builder-side guards and exact map indices.

## 1. Trigger pipeline

```
formkeydown / timer3ontimer / on-screen arrow buttons
        |
        v
      handan  (0x444490, raw-tile switch)
        |----------------------------> syounin / touzoku / yousei / roujin
        |
        v
      mevent  (0x46397c, floor-indexed map-event helper)
        |
        |----------------------------> zenolivework
        |
        v
     ichicheck  (0x46cb1c, floor 10..49 event dispatcher)
        |
        v
   floor event handlers (0x46cbc7 .. 0x4770a1)

Timer1Timer (0x443120) -----------------> takowork2 / takowork1
timer2timer (0x45265c) -----------------> stackwork (max 2 calls/tick)
stackwork 0xb ---------------------------> openning31 / zenolivework /
                                           madoushi2 / banpaiawork /
                                           moncheck / taisen2 / openning9
```

Important P2 finding: `ichicheck` is **not** called directly from a single
floor-arrival site.  It is called from 14 case bodies inside `mevent`
(`0x463bff`, `0x463c31`, `0x463c91`, `0x463e34`, `0x463e8b`, `0x463ebd`,
`0x463ec9`, `0x463efb`, `0x463f2d`, `0x463f84`, `0x463f90`, `0x463fc2`,
`0x463fcb`, `0x46401f`).  The caller-side condition for each call is part of
the `mevent` case body and must be lifted in WS3.

## 2. Input and movement entry points

| Caller | Address | Calls `handan` at |
|---|---|---|
| `formkeydown` | `0x460bd8` | `0x460cb1`, `0x460db8`, `0x460e64`, `0x460f12` |
| `timer3ontimer` | `0x461760` | `0x4617e3`, `0x461818`, `0x46184e`, `0x461882` |
| `BitBtn1Click` | `0x484b5c` | `0x484ba9` |
| `BitBtn2Click` | `0x484bb0` | `0x484bfb` |
| `BitBtn3Click` | `0x484c04` | `0x484c50` |
| `BitBtn4Click` | `0x484c58` | `0x484ca6` |

`handan` reads the target tile as:

```
raw = map[floor][target_index]
```

and dispatches on it.  Player x/y are serialized in the state block
(`0x4b86a0`/`0x4b86a4`); the target map index (`0x48c584`) and movement
direction (`0x48c590`) are separate auxiliary globals.  The trigger-oracle
snapshot must include both the serialized coordinates and those temporaries;
any other source-position value must be named explicitly.

## 2b. Entry-point ABI (partial)

All tracked event entries use the Delphi register convention: the first
argument arrives in `eax`.

* `stackwork` is published as `undefined __register FUN_004526c0(int *param_1)`.
* `handan`, `mevent`, `ichicheck`, `syounin`, `touzoku`, `roujin`, `yousei`,
  `zyouout`, `moncheck`, `taisen2` and the `openning*` helpers receive the main
  form pointer in `eax`.  `handan` stores it at `[ebp-4]` and uses the form's
  map/UI fields through `param_1[...]`.
* `Timer1Timer` is a Delphi timer event; the form/timer sender is passed in
  the register convention.  The exact `Self` vs `Sender` roles and required
  fields are not yet separated and must be recorded in P2.5.
* `timer2timer` calls `stackwork` twice with the same form pointer.

The complete set of required form fields, object pointers and auxiliary
globals is **not** yet enumerated.  P2.5 must turn the oracle entry/snapshot
contract into a fail-closed list before the pilot.

## 3. `handan` raw-tile families

The first switch (`jmp [eax*4+0x4444e3]`) covers raw `0x00..0x18` directly.
The relevant targets are:

| Raw | Target | Meaning |
|---:|---:|---|
| `0x00` | `0x448fbc` | no action / return |
| `0x01..0x03` | `0x4445d3` | yellow/blue/red door path |
| `0x06` | `0x4447c5` | move onto floor |
| `0x07` | `0x4447d2` | `mevent` case 7 path |
| `0x09` | `0x4447df` | `mevent` case 9 secret-wall path |
| `0x0a` | `0x4448ce` | `mevent` case 10 fixed-event path |
| `0x0b..0x0c` | `0x444cd8` | stairs up/down |
| `0x10` | `0x44509b` | special raw-`0x10` event |
| `0x13..0x14` | `0x445363` | `syounin` merchant |
| `0x15..0x16` | `0x445370` | `touzoku` thief |
| `0x17..0x18` | `0x44537d` | `yousei` fairy path |

Additional raw families:

| Raw | Target | Meaning |
|---:|---:|---|
| `0x19..0x1a` | `0x44538a` | `roujin` old-man path |
| `0x1b..0x1c` | `0x445397` | guarded map-event branch |
| `0x1d..0x1f` | `0x4454fb` | yellow/red/blue key pickup |
| `0x20..0x32` | table `0x445698` | consumables and special items |
| `0x33..0x3c` | table `0x445c91` | five swords and five shields |
| `0x3d..0x5a` | battle path | normal monster encounter |
| `0x5b..0x5d`, `0x61..0x69`, `0x73..0x7b`, `0x85..0x9d` | event/marker path | owner depends on floor/index |

The three decoded `handan` tables are:

| Table | Base | Entries | Index | Notes |
|---|---:|---:|---|---|
| `handan_raw_00_18` | `0x4444e3` | 25 | raw `0x00..0x18` | direct pointer switch |
| `handan_raw_20_32` | `0x445698` | 19 | raw `-0x20` | consumables/items |
| `handan_raw_33_3c` | `0x445c91` | 10 | raw `-0x33` | equipment |

## 4. `mevent` floor table

`mevent` (`0x46397c`) indexes a 50-byte floor table at `0x4639b6` and a
17-entry jump table at `0x4639e8`.  Non-default floors:

| Floor | Case target |
|---:|---:|
| 3 | `0x463a2c` |
| 10 | `0x463bd7` |
| 15 | `0x463c09` |
| 20 | `0x463c3b` |
| 23 | `0x463c9b` |
| 24 | `0x463e0c` |
| 25 | `0x463e3e` |
| 30 | `0x463e95` |
| 32 | `0x463ec7` |
| 33 | `0x463ed3` |
| 38 | `0x463f05` |
| 40 | `0x463f37` |
| 42 | `0x463f8e` |
| 43 | `0x463f9a` |
| 48 | `0x463fc9` |
| 49 | `0x463fd2` |

Case `0x464024` is the no-event return.  The per-case tile/index guards are
WS3 work; the table itself is fully decoded.

## 5. `ichicheck` floor table

`ichicheck` (`0x46cb1c`) indexes a 40-byte table at `0x46cb63` (index
`floor-10`) and a 15-entry jump table at `0x46cb8b`.  Non-default rows:

| Floor | Event index | Handler |
|---:|---:|---:|
| 10 | 1 | `0x46cbc7` |
| 15 | 2 | `0x46f82e` |
| 20 | 3 | `0x46f8f4` |
| 24 | 4 | `0x4704eb` |
| 25 | 5 | `0x4727b2` |
| 30 | 6 | `0x4728ef` |
| 32 | 7 | `0x472a2c` |
| 33 | 8 | `0x4735a4` |
| 38 | 9 | `0x47382c` |
| 40 | 10 | `0x473961` |
| 42 | 11 | `0x475fba` |
| 43 | 12 | `0x47687e` |
| 48 | 13 | `0x476cdd` |
| 49 | 14 | `0x476dc0` |

Event index 0 means “no event” and returns to `0x4770a1`.

## 6. `Timer1Timer` secret events

`Timer1Timer` (`0x443120`) has two hard-coded floor/tile/flag checks:

| Event | Floor | Map index | Tile check | Flag check | Handler |
|---|---:|---:|---|---|---:|
| floor-15 secret | 15 | `0x3c` | `0x4b90a7 == 6` | `0x4b88e8 < 2` | `takowork2` (`0x454a58`) |
| floor-35 secret | 35 | `0x47` | `0x4b9a4e == 6` | `0x4b88ec < 2` | `takowork1` (`0x4548a0`) |

Both increment their counters after the handler runs.

## 7. NPC floor tables

The old-man table (`roujin`, `0x449767`, floors `0..48`) is fully decoded by
the extractor.  Its non-default floors are:

```
2,3,4,6,16,18,21,23,27,31,33,36,37,39,42,45,46,48
```

Known old-man flags remain `0x42..0x45` for floors 2/3/4/6.  Merchant and
thief floors from the existing notes are:

| NPC | Floors |
|---|---|
| Merchant (`syounin`) | 2, 6, 7, 12, 15, 28, 31, 38, 39, 45, 47 |
| Old man (`roujin`) | 2, 3, 4, 6, 16, 18, 21, 23, 27, 31, 33, 36, 37, 39, 42, 45, 46, 48 |
| Altar (`zyouout`) | generic; called by event scripts, first altar on floor 4 |

`moncheck` (`0x4664e4`, floors `0..50`) also has a fully decoded floor table
(51 byte entries, 23 jump targets); its per-floor cases are monster-check
paths rather than map-contact triggers.

## 8. Event-script call graph (`0xb`)

`stackwork` sub-dispatch calls the event helpers:

| Sub | Callee |
|---:|---|
| 1 | `openning31` (`0x4653b8`) |
| 2 | `zenolivework` (`0x461a0c`) |
| 3 | `madoushi2` (`0x462560`) |
| 4 | `banpaiawork` (`0x47c978`) |
| 5 | `moncheck` (`0x4664e4`) |
| 6 | `gameover` (`0x454da0`) |
| 7 | title screen |
| 8 | `yusyahyouji` (`0x443648`) |
| 9 | `yusyahyouji1` (`0x4433a4`) |
| 10 | `taisen2` (`0x47f7d4`) |
| 0xb | floor change |
| 0xc | `openning9` (`0x482e60`) |
| 0xd | hide message/UI |
| 0xe | hide event images |
| 0xf | fade |

Opcodes `0x03` and `0x0a` also call `zyouout` (`0x449e18`).  These are
script-side dispatches, not input triggers.

## 9. Call-site counts

The extractor currently finds 91 direct call sites into the tracked event
entry points:

| Callee | Calls |
|---|---:|
| `zyouout` | 33 |
| `ichicheck` | 14 |
| `handan` | 12 |
| `gameover` | 6 |
| `mevent` | 3 |
| `zenolivework` | 3 |
| `takowork2` / `takowork1` | 2 each |
| `madoushi1` | 2 |
| `stackwork` | 2 |
| `banpaiawork` | 2 |
| `syounin`, `touzoku`, `yousei`, `roujin`, `openning2`, `openning31`, `madoushi2`, `moncheck`, `taisen2` | 1 each |
| `openning9` | 1 |

`timer2timer` contributes the two `stackwork` calls.

## 10. Confidence and open items

High confidence:

* input → `handan` call sites;
* `handan` raw-tile family table;
* `handan` → `mevent` / NPC call sites;
* `mevent` / `ichicheck` / `roujin` / `moncheck` switch tables;
* `Timer1Timer` floor-15/35 guards;
* `0xb` event-script call graph.

Medium confidence:

* the exact per-floor `mevent` case semantics and which cases call
  `ichicheck`;
* guarded raw-`0x1b/0x1c` branch and raw-`0x10` event;
* NPC repeat/one-shot branches beyond the confirmed old-man flags.

Still open:

* exact map-index and player-position guards for the `mevent`/`ichicheck`
  calls (WS3);
* event-flag map for one-shot/dialogue-stage conditions;
* floor/flag guards and behavior within the confirmed raw `0x17..0x18 ->
  yousei` and `0x19..0x1a -> roujin` paths;
* exact merchant/thief dialogue branches;
* event marker `0x5b..0x9d` per-floor owner table.

## 11. Reproduction

```sh
python3 docs/re/tools/scan_event_triggers.py "$GAME/TSW.exe" --summary
python3 docs/re/tools/scan_event_triggers.py "$GAME/TSW.exe" -o /tmp/opencode
python3 docs/re/tools/test_scan_event_triggers.py
```

Outputs:

* `event_switch_tables.tsv` — 244 decoded switch entries;
* `event_call_sites.tsv` — 91 direct call sites;
* `event_triggers.tsv` — 252 trigger rows (dispatcher rows + manual rows);
* `event_trigger_manifest.txt` — extractor name, binary SHA, pin status and
  row counts.
