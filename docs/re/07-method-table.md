# Published method table (TTSW10)

The full table is in [`method-table.tsv`](method-table.tsv): 201 entries with
`index`, runtime `address`, entry `size`, and Delphi method `name`.

The table lives at runtime `0x4419da` and has this binary layout:

```
Word  count
repeat count times:
  Word  size
  DWord address
  Byte  nameLen
  Char  name[nameLen]
```

This is **not** the layout that was used in the first exploratory parse.  The
correct pairing is `name` with the `size/address` pair that precedes it in
the stream, which is why early notes had `formkeydown` at `0x461760` and
`OptionSave1Click` at `0x47ecc4`.  The correct values are in the TSV.

## Key methods

| Name | Address | Notes |
|---|---:|---|
| `syokidata` | `0x455cb8` | new-game reset + embedded initial map |
| `syokidata2` | `0x454de8` | post-load/reset UI and state fixups |
| `handan` | `0x444490` | tile-action dispatcher (doors, items, events) |
| `idou` | `0x44c04c` | move player, step animation, adjacency checks |
| `disp` | `0x44cb34` | update floor/life/attack/defense/gold images |
| `mhyouji` | `0x442c38` | draw the 11×11 map |
| `formactivate` | `0x44280c` | form activation / initial render |
| `Timer1Timer` | `0x443120` | animation tick + floor 15/35 events |
| `taisen` | `0x44a480` | battle resolution/display |
| `monshyouji` | `0x44b78c` | monster info window |
| `keydisplay` | `0x44bed8` | key grid |
| `itemdisp` | `0x44ccd8` | equipment + inventory grid |
| `itemlive` | `0x450880` | use/consume item |
| `itemdel` | `0x450758` | hide item UI |
| `formkeydown` | `0x460bd8` | real keyboard handler |
| `timer3ontimer` | `0x461760` | mouse-arrow repeat handler |
| `timer2timer` | `0x45265c` | second animation/input timer |
| `mevent` | `0x46397c` | stairs / map transition |
| `ichicheck` | `0x46cb1c` | per-floor event dispatcher |
| `doorcheck` | `0x47beac` | door state check |
| `dooropen` | `0x4770e0` | door animation |
| `doorstackin` | `0x4660d0` | door-opening frame sequence |
| `yousei` | `0x478f0c` | fairy score, ranking, `save0.dat` |
| `syounin` | `0x449014` | merchant |
| `zyouout` | `0x449e18` | altar |
| `roujin` | `0x449708` | old man |
| `touzoku` | `0x465900` | thief |
| `madoushi1` | `0x44c464` | wizard |
| `madoushi2` | `0x462560` | wizard variant |
| `banpaiawork` | `0x47c978` | vampire |
| `gameover` | `0x454da0` | death / new game reset |
| `savework` | `0x47eadc` | write save file |
| `loadwork` | `0x47e7d4` | read/validate save file |
| `OptionSave1Click` | `0x47d324` | write `option.dat` |
| `Save11Click` | `0x47ecc4` | save slot 1 (… `Save81Click`) |
| `Load11Click` | `0x47d9d4` | load slot 1 (… `Load81Click`) |
| `GameStart1Click` | `0x4637e8` | new-game confirmation |
| `GameQuit1Click` | `0x463874` | quit confirmation |
| `HighScore1Click` | `0x482988` | ranking form |

## Event-handler addresses

Because these are only referenced from the method table, Ghidra's auto-analysis
does not create functions for all of them.  Use `get_disassembly`/`objdump`
at the address (or create a function in Ghidra) when decompiling:

```
formkeydown         0x460bd8
timer3ontimer       0x461760
button8onmousedown  0x4618fc
button8onmouseup    0x461944
button9onmousedown  0x461984
button9onmouseup    0x4619cc
GameStart1Click     0x4637e8
GameQuit1Click      0x463874
OptionSave1Click    0x47d324
Save11Click         0x47ecc4
Save21Click         0x47edb8
...
```

## Naming caveat

The Delphi method names are often abbreviated Japanese romaji (e.g.
`syokidata` = initial data, `idou` = movement, `handan` = judgment,
`taisen` = battle, `yousei` = fairy).  They do not always match the modern
English role exactly.  Use the behavior in the notes, not the name alone.
