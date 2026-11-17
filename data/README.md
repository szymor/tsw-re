# Game data

Everything the game loads at run time, committed so that a build needs
only a C compiler and SDL3.  CMake installs this folder as `share/tsw/`
(with `src/events/` as `share/tsw/events/`).

| Folder | What | Made by |
|---|---|---|
| `gfx/` | panels, tiles, sprites, the ending's pictures, the icon (PNG) | `tools/convert_assets.py` from the original `Data/`, `Data2/` and `TSW.exe` |
| `audio/` | the sounds (WAV, as the original's) and the tunes (Ogg Vorbis, rendered from the original MIDI with FluidSynth) | `tools/convert_assets.py` |
| `maps/initial.bin` | the New Game map block (`syokidata`) | `tools/extract_maps.py` |
| `text/<lang>/` | `messages.tsv`, the dialogue list (the original's `ListBox2`, same indices), and `ui.tsv`, the literals of the code | extracted once by `tools/extract_messages.py`; **edit these files directly** |
| `fonts/` | Liberation Sans Regular and Bold | copied from the Liberation fonts 2.1.5 |

The pictures, sounds and tunes come from *Tower of the Sorcerer* 1.2 (Oz &
Kenichi), whose art is that of NAO's original *Mahō no Tō*; NAO allows fan
works based on the game.  The converters are kept in `tools/` as the
record of how each file was made, and to regenerate the files after a
converter fix (`tools/check_assets.py --game-dir` compares them with the
originals).

Liberation Sans is under the SIL Open Font License 1.1
(`fonts/LICENSE-LiberationSans.txt`).

## Text

One line per entry, `<index>\t<text>`, with backslash, tab and newline
escaped as `\\`, `\t` and `\n`.  The index is the original's, so it must not
change; a translation is a copy of `en/` under another language code with
the text translated line by line.  The game reads `MESSAGES_LANG`
(`src/messages.h`, English for now).
