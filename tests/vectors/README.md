# The original's results

What the original `TSW.exe` did, run in an x86 emulator
(`docs/re/tools/native_event_oracle.py`), for the tests to compare the
remake with.  Made by:

```sh
python3 docs/re/tools/export_event_vectors.py --repo
```

which needs the original game and Python with Unicorn 2.1
(`docs/re/tools/README.md`); a build and the tests need neither.

| Files | What | Read by |
|---|---|---|
| `*.vec` | one event or map action each: the input state, map and auxiliaries, the commands the original appended, the state, map and auxiliary changes, the status bar, and what its timeline did when run to the end (dialogues, final state and map) | `test_event_builders` |
| `monsters.tbl` | the monster table (`0x489910`) | `test_original` |
| `*.mapdraw` | `mhyouji` drawing a floor: the tile of every cell in both animation frames | `test_original` |
| `*.sav` | `savework` writing a game, `loadwork` reading it back and refusing it with one byte changed | `test_original` |
| `ui.tsv` | the code's literals (`" point up!"`, the joiners) | `test_event_builders` |

The format of a `.vec` file is described at the top of
`export_event_vectors.py`.  A plain export (without `--repo`) writes the
same and more to `~/.cache/tsw/vectors`: a `.json` file per vector for the
analysis tools, and the player's own saves from the game's `Savedat/`.
Point the tests at such a folder with `TSW_EVENT_VECTORS=<folder>`.

This is the original game's material (its code's behaviour, its maps and
text indices), like `data/`: not under the program's GPL (see the README's
*Licence* section).
