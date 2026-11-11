# Binary and assets

## Executable

| Property | Value |
|---|---|
| File | `TSW.exe` |
| Format | PE32, i386 |
| Compiler/runtime | Delphi 2.0 (VCL, 32-bit) |
| Image base | `0x400000` |
| Code section | RVA `0x1000`, size `0x87e94` |
| Data / BSS | RVA `0x89000` … `0xbb1b8` |
| Resources | RVA `0xc7000`, 90 entries |

The executable is a single-file Delphi application.  Almost all game data
that is not in `Data*/` or `Savedat/` is embedded in the code or in the
form resources (DFM).

## Delphi forms

The PE resource section contains five `RT_RCDATA` Delphi form streams:

| Resource | Meaning |
|---|---|
| `TTSW10` | main game form (564,101 bytes) |
| `TFORM2` | ranking/high-score form |
| `TABOUTBOX` | about box |
| `TFORM4` | small dialog |
| `TOKBOTTOMDLG` | message dialog |

`TTSW10` contains the menu, the 11×11 map image components, the status
images, the key grid, the 15 item buttons, and a large string list of game
messages.  It is the source of all UI text.

## Published method table

Delphi stores a published-method table for each form.  For `TTSW10` it is a
contiguous table at runtime `0x4419da`:

```
Word  count = 0x00c9 (201 entries)
repeat
  Word  size          // total entry size in bytes
  DWord address       // runtime VA of method
  Byte  nameLen
  Char  name[nameLen]
until count entries
```

The table was parsed from the executable.  `docs/re/method-table.tsv` contains
all 201 `name → address` pairs.  Examples:

| Name | Address | Role |
|---|---:|---|
| `syokidata` | `0x455cb8` | new-game / reset initializer and embedded map |
| `handan` | `0x444490` | tile-action handler (doors, items, events) |
| `idou` | `0x44c04c` | move player / step animation |
| `disp` | `0x44cb34` | update stat/status images |
| `mhyouji` | `0x442c38` | draw 11×11 map |
| `taisen` | `0x44a480` | battle resolution and battle display |
| `keydisplay` | `0x44bed8` | draw up to 24 keys in a 6×4 grid |
| `itemdisp` | `0x44ccd8` | draw equipment and 15-slot inventory |
| `formkeydown` | `0x460bd8` | real key handler (arrows, Enter, Esc) |
| `timer3ontimer` | `0x461760` | mouse-arrow repeat/movement timer |
| `Timer1Timer` | `0x443120` | animation and special-event tick |
| `savework` | `0x47eadc` | write save file |
| `loadwork` | `0x47e7d4` | read/validate save file |
| `yousei` | `0x478f0c` | fairy score, ranking, `save0.dat` checkpoint |
| `gameover` | `0x454da0` | reset after death / new game |
| `GameStart1Click` | `0x4637e8` | new-game menu action |

> Important: early analysis used a mis-aligned parse of the method table.
> The corrected mapping is the one in `method-table.tsv`.  In particular
> `handan` is the big tile-action routine and `disp` is the stat updater.

## Assets

```
Data/                 original 640×400-era assets
Data2/                ending images (End05..End26)
Data3/                alternate/extra assets
  Chtable.oz          compressed 32×32 tile atlas
  Chtableb.oz         alternate tile atlas
  Key.oz              key icons
  Left.bmp, LeftR.bmp left status panel
  Right.bmp, RightR.bmp right status panel
  CENTER.BMP          center background
  open0.bmp..open2.bmp door-opening frames
  title05.bmp         title image
Midi/                 BGM tracks (MIDI)
Savedat/              save slots, options, rankings
```

The `.oz` files are compressed atlases.  The extracted tile atlas used during
analysis is `Chtable.png` (not committed).  Sprite indices used by the map are
`raw tile value - 1`; tile 5 is the ordinary floor, 6/7/8/9 are walls, etc.
See `02-map-format.md`.

## Runtime layout (selected)

| Address | Meaning |
|---:|---|
| `0x4b8688` | start of the 684-byte state block |
| `0x4b8698` | current floor (1-based) |
| `0x4b86a0` / `0x4b86a4` | player x / y (0..10) |
| `0x4b8934` | map block header (`00 01`) |
| `0x4b8936` | map data base (map index 0) |
| `0x4b89b1` | floor 1 map (`base + 123`) |
| `0x489910` | monster table, 33 records × 16 bytes |
| `0x489b1f` | 121-byte render-order table for the map |
| `0x4b88fc` | save-format magic (`-4`) |
| `0x4b8918` / `0x4b891c` | state checksums |
| `0x4b8930` | save format version (12) |
| `0x48c5d4` | current save-file path buffer |
| `0x48c600` | Delphi file-stream object |

## Tooling notes

* Ghidra project: `/TSW.exe`; use runtime VAs as function addresses.
* `objdump -d -M intel` is useful for functions Ghidra did not auto-create
  (event handlers are only referenced from the published method table).
* Full memory dump: `/tmp/opencode/mem/` (rw regions + TSW.exe headers).
* Nested display: `DISPLAY=:2`; game window `0xc00004` is 642×443 at `+3+29`.
* Synthetic XTest key/mouse events work; arrow keys and mouse arrow buttons
  both move the player.
* The menu did not respond to synthetic clicks or Alt shortcuts in the
  analysis session; save/load behavior was reconstructed statically.
