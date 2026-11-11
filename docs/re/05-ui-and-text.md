# UI, menus and text

## Main window layout

The main form's client area is 640×400 (the X window is 642×443 because of
the menu bar and frame).  The layout is:

```
+--------------------------------------------------------------+
| Game  Options  Help                                          |  menu bar
+----------------+-----------------------------+---------------+
| left panel     | 11×11 map (352×352)          | right panel   |
|  heart/sword/  |                             | WEAPON        |
|  shield icons  |                             | SHIELD        |
|  stat images   |                             | item detail   |
|  message box   |                             | key grid      |
|                |                             | arrow pad     |
+----------------+-----------------------------+---------------+
| status bar (blue)                                            |
+--------------------------------------------------------------+
```

Asset dimensions: `Left.bmp` 125×400, `CENTER.BMP` 390×400,
`Right.bmp` 125×400 (125 + 390 + 125 = 640).  The 352×352 map is centered in
the 390 px center panel, leaving 19 px side margins and 24 px top/bottom
margins.  The 640×480 PoC keeps this 640×400 playfield unscaled and adds an
**80 px bottom console**; there is **no permanent menu bar**.  The game is
fully keyboard-controllable and menus are keyboard overlays.

Key components (from the DFM and the update routines):

| Component / form offset | Role |
|---|---|
| `Image1..Image5` | left panel, map, right panel backgrounds |
| `Image13..Image23` | stat/status images |
| `Image33` | large map image |
| form `+0x3f0` | floor label image |
| form `+0x3c8`, `+0x3cc`, `+0x3d0`, `+0x3d4` | life/attack/defense/gold image labels |
| form `+0x1c8`, `+0x288`, `+0x28c` | weapon/shield name and icons |
| `+0x1dc`, `+0x1ec`, `+0x254` … `+0x284` | 15 item buttons/images |
| `+0x1b4` | message/status panel |
| `+0x444`, `+0x44c` | Delphi string lists / resource lists |

`disp` (`0x44cb34`) updates the stat images after every action.  `itemdisp`
(`0x44ccd8`) redraws the equipment and the 15-slot inventory (3 columns ×
5 rows).  `keydisplay` (`0x44bed8`) redraws the key grid (6 columns × 4 rows).

> Offset conflict: `OptionSave1Click` (`0x47d324`) also reads components at
> `+0x3c8` and `+0x1c8`, the same offsets that `disp` uses for the life and
> weapon-name images.  The exact identity of the option controls is therefore
> unresolved; do not rely on the `option.dat` field mapping yet.

## Menus

The menu bar is a real Delphi `TMainMenu`:

| Menu | Item | Shortcut | Handler |
|---|---|---|---|
| Game | Game &Start | F2 | `GameStart1Click` (`0x4637e8`) |
| Game | Game &Quit | F9 | `GameQuit1Click` (`0x463874`) |
| Game | &Ranking | — | `HighScore1Click` (`0x482988`) |
| Game | Game &Save | — | submenu: `Save11Click` … `Save81Click` |
| Game | Game &Load | — | submenu: `Load11Click` … `Load81Click` |
| Game | E&xit | — | `Exit1Click` (`0x46391c`) |
| Options | &Save (options) | — | `OptionSave1Click` (`0x47d324`) |
| Options | &Sound / &BGM | — | `wavon1Click`, `BGMON1Click` |
| Options | speed / display | — | `High1Click`, `Middle1Click`, `Low1Click`, `Large1Click`, `Small1Click`, `Normal1Click`, `tiny1Click`, `NormalOption1Click` |
| Options | &Keyboard / &Mouse | — | `KeyboardControl1Click`, `MouseControl1Click` |
| Options | 640×400 / 800×500 | — | `N640x4001Click`, `N800x5001Click` |
| Help | &About | — | `About1Click` (`0x47d318`) |
| Help | &Help | — | `Help2Click` (`0x47d2d8`) |

The exact menu nesting is inferred from the DFM item names and captions; the
handler addresses are from `method-table.tsv`.  In particular, **Game Save**
is the eight-slot save submenu, while **Options → Save** writes
`option.dat` via `OptionSave1Click`; they are not the same action.

> The PoC does **not** draw a permanent menu bar.  The actions above are
> available from a keyboard menu overlay (proposed `F10`), with `Esc` to
> cancel/close, `F2` new game and `F9` quit.

`GameStart1Click` asks “This game is over. Is it true?”.  On Yes it clears the
option flags and calls `gameover`, which resets the game via `syokidata`.

The game uses Delphi's accelerator table, so in the original Windows build
`Alt+G`/`Alt+O`/`Alt+H`, `F2` and `F9` work.  In the nested Wine analysis
session, synthetic menu clicks and Alt accelerators did not open the menu,
although arrow keys and mouse arrow buttons worked.  This is an environment
issue, not a game-logic issue; the handlers were reconstructed statically.

## Text rendering and the font quirk

All 74 DFM font properties are `Verdana`.  The stat/status boxes are
`TImage` components; the game draws strings onto their canvases using the
VCL font rather than `TLabel` captions.

Under the analysis Wine prefix the following was observed:

* The menu text renders (Wine's menu font).
* The map, sprites and backgrounds render.
* The stat/message image text is **not visible**: the boxes stay white.

A `Verdana` font was installed (a renamed DejaVu Sans) and Wine's font
replacement registry key was set.  Wine's `+font` trace confirms that the game
selects `Verdana` and calls `NtGdiExtTextOutW` with strings such as `"100"`
and `"1000"`, but the visible pixels remain white.  The likely cause is the
text color/background combination or a VCL canvas quirk under Wine, not a
missing font.  This does **not** block the analysis: the values are read
directly from the state block.

For the re-implementation, do not depend on VCL image text.  Render text with
SDL_ttf (or a small bitmap font) into the status/inventory panels.

## Game text

`TTSW10` contains a large Delphi string list with the game messages, NPC
dialog, item names and endings.  The game code refers to messages by numeric
index (e.g. `0x89`, `0x98..0xa6`) through the VCL string-list helpers.

Known message groups:

* Item pickup messages (`You've found ...`, `Got an Iron Sword! ...`).
* NPC dialog (old man, merchant, thief, fairy, Zeno, princess).
* Door/key messages (`The blue door has opened.`,
  `You don't have the red key.`).
* Stairs/movement (`Went up the stairs.`, `Went down the stairs.`).
* Battle and death messages.
* Endings (`End05.bmp` … `End26.bmp`).

The raw extraction is generated locally into `data/text/` (gitignored) and
is not committed.  Refined original text will be committed under `text/`
once the message format is stable.  A robust DFM string-list extractor is
still needed; `tools/extract_text.py` is the starting point.

## Save/load UI

The save/load menu handlers set the path buffer `DAT_0048c5d4` and call
`savework` / `loadwork`.  The file names are `save1.dat` … `save8.dat`.
`save0.dat` is the fairy checkpoint and is written by `yousei`, not by the
save menu.

The path buffer is built from the application directory plus `\saveN.dat`.
The `Savedat/` directory in the installation contains the sample/current
files.
