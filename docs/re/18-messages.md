# Dialogue messages: one list, two index rules

Every dialogue the event code shows comes from a single Delphi string list, and the
index rules are recovered from `zyouout`.  `tools/extract_messages.py`
extracted it once into `data/text/en/messages.tsv`, which is committed and
is now the text to edit and translate (`data/README.md`); the extractor's
output goes to the cache, for comparison.

## 1. The list

`zyouout` (`0x449e18`) reads every dialogue from
`TStrings.Get` (vtable `+0x0c`) on the `Items` of the component at **form
field `+0x44c`**.  The `TTSW10` published-field table and the form resource
name it:

| Form field | Class | Name | Strings |
|---|---|---|---:|
| `+0x44c` | `TListBox` | **`ListBox2`** | 268 |
| `+0x1cc` | `TRichEdit` | `RichEdit1` (the dialogue window) | 1 |
| `+0x1d0` | `TButton` | `Button1` (its OK button, `Button1Click`) | — |
| `+0x444` | `TMemo` | `Memo12` | 1 |

`docs/re/tools/read_dfm.py` parses the binary DFM (`TPF0`) and the field
table.  TSW.exe uses the **Delphi 2 VMT layout**: the class-name slot is at
VMT − 28, and the type-info, field-table and method-table pointers are at
−16, −12 and −8 from it (no `vmtSelfPtr`).  Field-table class references
point at the referenced class's VMT.

```sh
python3 docs/re/tools/read_dfm.py "$GAME/TSW.exe" --fields     # offset -> component
python3 docs/re/tools/read_dfm.py "$GAME/TSW.exe" --lists      # string-list properties
python3 tools/extract_messages.py --game-dir "$GAME"           # -> ~/.cache/tsw/text/messages.tsv
```

## 2. Index rules (`zyouout`)

`g_subevent_id` is `0x48c5a4`; the dialogue selector `g_dialog_selector` is
`0x48c5a0`; `0x48c5c8` is a **word** holding opcode `0x0a`'s `arg1` (its high
word `0x48c5ca` is `itemlive`'s flag, set by `itemlive` `0x450b43`, cleared by
`itemdel` `0x450758`).

| Case | Text |
|---|---|
| `g_subevent_id == 9` | no text: appends a game-over timeline (`0x16`, `0x0a/0x5d`, …) and calls `timer2on` |
| `g_subevent_id < 10` (NPC dialogues: `roujin`, …) | `ListBox2[g_dialog_selector + 0x3e]` |
| otherwise (opcode `0x0a`, id = arg1 + 10) | `ListBox2[arg1 − 1]`, except arg1 ∈ {`0x10`, `0x16`, `0x18`, `0x28`, `0x2f`} (no list text) |
| arg1 = `5` / `0x11` / `0x37` | additionally **appends** `ListBox2[arg1 + 0x34]` / `[arg1 + 0x29]` / `[arg1 + 4]` (`LStrCat` `0x403658`) |
| `state[0xa7] != 1`, arg1 = `0x5d`, id ≥ 10 | Yes/No `MessageDlg` (literal at `0x44a454`, extracted as `data/text/en/ui.tsv` entry 1) instead of the window |

The window (`RichEdit1`) and `Button1` are made visible afterwards; the
confirm is `Button1Click`.

`zyouout` never sets `Button1`'s caption.  The event code sets it
beforehand (`TControl.SetText` `0x4135d4` on form `+0x1d0`) and it stays until
the next change:

| Caption | Set by |
|---|---|
| `ListBox2[0xda]` ("What?") | `mevent` floor 3 (`0x463a75`); `ichicheck` floors 20, 25, 32, 40, 49 |
| `ListBox2[0xdb]` ("Hey!") | `openning2` (`0x46409b`): the capture, sub-event 21 |
| `'OK'` (the form's default) | `openning31` (`0x4655cc`), `syounin`, `roujin`, `touzoku`, `yousei`, `moncheck`, `handan` (×2), `ichicheck` floor 24 (`0x4704fb`) |

The remake's builders send these as `present("button_caption"[, index])`.

## 3. Resolved dialogue IDs

| Source | Selector | `ListBox2` index | Content (identifying start) |
|---|---|---:|---|
| floor-3 trigger, sub-event 20 (`0x0a/10`) | arg1 10 | 9 | Zeno's speech |
| `openning2`, sub-event 21 (`0x0a/11`) | arg1 11 | 10 | "What? Oh!" |
| `openning31`, sub-events 11, 11, 12, 13 | arg1 1, 1, 2, 3 | 0, 0, 1, 2 | the prison wake-up ("------", "… Hey!", "… Hey! Wake up!") |
| floor 10, sub-event 23 (`0x0a/13`) | arg1 13 | 12 | Skeleton A's challenge (the area-1 boss) |
| `yousei`, sub-event 26 (`0x0a/0x10`) | arg1 `0x10` | — | excluded: no list text (the fairy's window text is set elsewhere) |
| `roujin` floor 2 | `0x19` | `0x57` | the 3 % boost |
| `roujin` floor 3 | `0x1a` | `0x58` | the Orb of the Hero |
| `roujin` floor 4 | `0` | `0x3e` | doors that open by beating monsters |
| `roujin` floor 6 | `1` | `0x3f` | buy from merchants |
| `roujin` floor 16 (x = 9) | `0x1b` | `0x59` | (Magic Elixir gift) |
| `roujin` repeat | `0x18` | `0x56` | "I have done what I can. Good luck!" |

The captured-live texts that were once hard-coded in the removed
`events.lua` match these entries; the engine now reads the list instead.

## 4. Engine

`src/messages.c` loads `data/text/en/messages.tsv` (`<index>\t<escaped text>`);
`src/event_engine.c` implements the rules of section 2 when opcode `0x0a`
runs or a builder calls `present("zyouout")`, shows the window, and runs the
`button1click` builder on Enter.  Without the file the window shows
`[message N]` placeholders and the game stays playable.

### Speakers (remake)

The original names no speaker: a few story lines start with one ("The
Golden Knight says "...""), the rest leave it to the scene.  The remake
puts the speaker in a middle column of `messages.tsv` (`data/README.md`)
and shows their name and animated map sprite in the window; the "X says"
prefixes of entries 9, 40-45, 47-56, 59 and 61 are dropped with their
quotes.  `append_message` records the speaker of the first entry a window
gets, so zyouout, RichEdit1 texts (merchants, the altar) and the fairy's
score text all follow it.  The attribution comes from the event code
(sub-event arg1 - 1, the old men's selector + 0x3e, the merchants'
offer and thank-you entries) and the map:

| Speaker | Entries | Icon (Chtable frames A/B) |
|---|---|---|
| hero (brave man) | 10, 25, 47, 49, 51, 53, 55 | Player.png, facing down |
| thief | 0-6, 16, 17, 26-28, 31, 32, 48, 57, 58, 263 | 0x14 / 0x15 |
| Zeno | 9, 41, 43-45, 50, 52, 54, 56, 59, 61 | 0x5a / 0x5b |
| Golden Knight (floors 32, 40, 42) | 29, 30, 33-38, 40, 42 | 0x84 / 0x85 |
| Skeleton A (floor-10 boss) | 12-14 | 0x50 / 0x51 |
| Vampire | 19, 20 | 0x5e / 0x5f |
| Great Magic Master | 22 | 0x90 / 0x91 |
| old men | 7, 11, 18, 60, 62, 63, 68-72, 74-76, 78, 80, 81, 83, 85-89, 264 | 0x18 / 0x19 |
| merchants | 8, 64-67, 73, 77, 79, 82, 84, 90, 91, 246-259 | 0x12 / 0x13 |
| fairy | 220-236, 244, 245 | 0x16 / 0x17 |
| altar | 202-211 | 0x0f / 0x11 (the middle tile) |

Entry 24 ("Now is the time. You were chosen...", after the floor-25
boss) has no speaker: the scene does not say who talks.  Entries 7, 8,
11 and 18 look unused (near copies of 87-89 and 246).

Entries 77 (the floor-38 merchant's thank-you line) and 83 (the floor-46
old man) trail off on purpose: the PC-98 original ends them with `……。`
(`だから……。`), and the Windows translation's bare comma read like a cut
sentence.  The remake keeps the ellipsis in all five languages.

### Entries added by the remake

Entries from 268 on are not in the original list.  268 ("Lost"), 269
("vital power, received") and 270 ("without a scratch.") extend the won
fight's status line with the vital power the fight cost (`victory_text`
in `src/events/battle.lua`): "You defeated Green Slime. Lost 35 vital
power, received 5 Gold.", or "...without a scratch. Received 5 Gold."

## 5. Open items

* Sub-event 9 (game over) and the `0x5d` life-point choice are not ported.
* The fairy's window text (arg1 `0x10`) and the other excluded selectors
  (`0x16`, `0x18`, `0x28`, `0x2f`) are filled by their handlers; map them with
  those handlers.
* Non-dialogue text (item pickups, doors, battle results in the status
  panel) uses other lists or literals; map them with their handlers.
* `text/` (refined, committed text) is still planned for a later localisation
  layer; the runtime list stays generated.
