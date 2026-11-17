# The original's windows

The game has no window graphics of its own: every dialogue is a standard
Delphi 2 control (`TRichEdit`, `TEdit`, `TButton`, `TPanel`, `TBitBtn`) on
the main form `TTSW10` or on the ranking form `TForm2`, drawn by Windows 95
(Ctl3D).  The DFM gives each control's design-time bounds, but most are moved
at run time: `syokidata0` lays the form out for the chosen size
(`g_option_dat[8]`: 0 = 640x400, 1 = 800x500), and the window openers
(`zyouout`, `img6work`, `img8work`.., `Button2Click`, `handan`) move them
again.  `tools/form_layout.py` runs that code in Unicorn on a model of the
form and prints the final bounds; the remake places its windows there
(`src/render_windows.c`, `src/widgets.c`).

## The remake's presentation

The remake keeps the positions but not the Windows 95 look, and it is
played with the keyboard only, so rows of buttons become choices:

* plain dark panels with a thin outline, one sans face (12/14/16 px);
  in-game windows leave the map visible, separate dialogs (MessageDlg,
  ShowMessage, Form2, InputQuery) dim the screen;
* the message text (`RichEdit1`: event messages and the Orb of Wisdom's
  words) is justified, the last line of a paragraph left-aligned
  (`src/text_layout.c`); the original's `TRichEdit` is left-aligned;
* the chosen answer has a gold highlight bar and a dim line names the
  keys; Button1 shows the event's caption ("OK", "What?", "Hey!");
* two-way questions (merchant, item use, "try again?") open on the
  no-action answer (No, "Don't use"); Left/Right pick a side, Tab
  toggles, Y/N answer directly, Esc is the no-action answer.  The
  original focuses Yes for the merchant and the prompt and "Don't use"
  for items;
* the altar's offers are a list (Up/Down, 1-3, Esc cancels);
* the Orb of Flying shows the floor with the open directions;
* the Orb of Wisdom lists the men heard; the highlighted man's words show
  at once (his `Button11..34Click`), Enter closes (`Button35Click`);
* Form2's area buttons are tabs (Left/Right show the other areas' tables
  read from their files); OK and Clear are not shown (Enter closes).

## The layout oracle

`form_layout.py` builds a form whose published fields point at one object
per control, with `Left/Top/Width/Height` at `+0x24/+0x28/+0x2c/+0x30` from
the DFM.  It executes the handler, performs `TControl.SetLeft/SetTop/
SetWidth/SetHeight` (`0x413098/0x4130b8/0x4130d8/0x4130f8`) and the client
width/height getters (`0x413178/0x4131bc`) and `SetClientHeight`
(`0x42151c`; `syokidata0` sets 424, so `Edit8` is 24 high), and skips every
other call.  A bound that depends on a skipped call (a text metric, say)
would come out wrong, so results are checked against the panel art.
`--state INDEX=VALUE` sets state words first (the item handlers test the
item), `--then` runs window openers after `syokidata0`, `--form TForm2` runs
Form2's `formactivate` (`0x434bc4`) instead.

```
form_layout.py TSW.exe --then zyouout
form_layout.py TSW.exe --state 0x11=1 --then img6work
form_layout.py TSW.exe --form TForm2
```

## Main form, 640x400 layout

| Window | Controls and bounds (left, top, width x height) | Set by |
|---|---|---|
| message window | `RichEdit1` (129, 45) 382x200, `clGrayText`, white Verdana at option byte 1 (12 pt); `Button1` (420, 204) 75x25 | `zyouout` |
| merchant / altar question | `Button7` "No" (420, 204), `Button2` "Yes" (329, 204), 75x25 | `handan` |
| altar menu | `Button39..42` (145, 93 + 32k) 350x32, font -17; focus `Button42` | `Button2Click` mode 1 |
| item use | `Button37` "Don't use" (focus) at (icon.left - 16, icon.top + 32) 64x16, `Button38` "Use" under it | `img8work`..`img17work` |
| Orb of Flying | `Button8` "UP" (70, 224), `Button9` "DOWN" (70, 240), `Button10` "OK" (70, 256), 48x16 | `syokidata0` |
| Orb of Wisdom | `Button11..34` "1".."24" (14 + 16c, 224 + 16r) 16x16, six per row; `Button35` "OK" (78, 288) 32x16 | `syokidata0` |
| Orb of the Hero | `Panel1` (125, 0) 390x397 `clGrayText`; `RichEdit1` (141, 16) 358x56 with `ListBox2[0xb4]`; row i (y = 80 + 40i): picture (141, y + 4), `Edit13..` name (181, y) 114x20, `Edit27..` damage (295, y) 204x20 red, `Edit14..` stats (181, y + 20) 318x20 (edits -13 bold); `Button36` "OK" (435, 368) 64x20 | `img6work` |
| left-panel items | `Image4..18` at (14 + 32c, 192 + 32r) | `syokidata0` |
| status memos | `Memo11` floor (11, 45) 103x24 centred; `Memo1..4` life/offense/defense/gold (38, 78 + 24k) 76x20 right-justified; `Memo9/10` weapon/shield (524, 64 / 112) 68x20 centred | `syokidata0` |
| ending button | `Panel2` (454, 356) 184x40, `Image34` (2, 2) 180x36 inside | `syokidata0` |

The Orb of Wisdom's `Button11Click..Button34Click` (`0x44ee4c`..) set
`g_dialog_selector` to 0..23 and `state[0xa7]` to 1 and call `zyouout`,
which shows `ListBox2[k + 0x3e]` in `RichEdit1` but not `Button1` (it only
shows `Button1` while `state[0xa7]` is 0).  Button k is enabled when the
old man's flag is 2, in this order: `0x44, 0x45, 0x37, 0x38, 0x39, 0x3b,
0x46, 0x48, 0x49, 0x4a, 0x4b, 0x3d, 0x4c, 0x4d, 0x4e, 0x3e, 0x4f, 0x3f, 0x50,
0x51, 0x40, 0x52, 0x41, 0x53` (`img5work`).  `Button35Click` hides the text
and the buttons, clears `g_subevent_id` and `state[0xa7]`, and runs
`itemlive`.

The Orb of the Hero's rows (`yousya`): the name (`monswork`), then
`ListBox2[0xd6]` hp `ListBox2[0xd7]` attack `ListBox2[0xd8]` defense, each
times (difficulty + 1) and separated by a space, except that the sixth row
leaves out the space after `ListBox2[0xd7]` (`LStrCat` with nil); the damage
is `ListBox2[0xb8]` + " " + damage, or `ListBox2[0xb9]`.  Empty rows show
the panel tile `0xa2`.

`MessageDlg` (`0x42cec8`): the "try again?" prompt is `mtInformation`
(caption "Information") with Yes/No.  `ShowMessage` uses
`Application.Title`, "Tower of the Sorcerer Ver1.2" (`0x488e74`).

## Form2 (ranking)

`formactivate` (`0x434bc4`) resizes the form to 480x444 (screen-centred)
and lays it out again:

| Control | Bounds (client) |
|---|---|
| `Memo61` "RANKING of Top 10" (no border, -16 bold) | (16, 8) 442x28 |
| `BitBtn1..5` "10F".."49F" with the area's glyph | (49 + 80i, 44) 56x56 |
| `Label1..6` Rank, Points, Name, Life, Offense, Defense (disabled) | y 108, x 16/64/127/279/349/404, widths 40/55/152/70/55/55 |
| ten rows of memos (-12 bold) | y 126 + 25r, the label columns, height 25 |
| `Button6` "OK", `Button1` "Clear" | (128, 384) and (256, 384), 96x25 |

The rank column holds "1".."10" (Form2's create handler); the first row is
`clGrayText` with red text, the others white with black (the DFM's
colours).  `FUN_00437f2c` gives the shown area's button the focus.  The
name prompt is `InputQuery("Input your name", "Please input within 18
words")`.  `tools/convert_assets.py` extracts the five glyphs
(`TBitBtn.Glyph.Data`, bottom-left pixel transparent) as
`RankGlyph1..5.png`.
