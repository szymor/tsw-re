# Game data

Everything the game loads at run time, committed so that a build needs
only a C compiler and SDL3.  CMake installs this folder as `share/tsw/`
(with `src/events/` as `share/tsw/events/`).

| Folder | What | Made by |
|---|---|---|
| `gfx/` | panels, tiles, sprites, the ending's pictures, the icon (PNG) | `tools/convert_assets.py` from the original `Data/`, `Data2/` and `TSW.exe`; `title05_clean.png` and `title05R_clean.png`, the title pictures without their credit lettering (the game writes the credits itself), by `tools/make_clean_titles.py`; `Image33_clean.png` and `Image34_clean.png`, the ending's pictures without their English (the game writes it in the player's language, `src/pictures.c`), by `tools/make_clean_pictures.py` |
| `audio/` | the sounds (WAV, as the original's) and the tunes (Ogg Vorbis, rendered from the original MIDI with FluidSynth) | `tools/convert_assets.py` |
| `maps/initial.bin` | the New Game map block (`syokidata`) | `tools/extract_maps.py` |
| `text/<lang>/` | `messages.tsv`, the dialogue list (the original's `ListBox2`, same indices, with who speaks each line), `speakers.tsv`, the speakers' names, `ui.tsv`, the original's code literals, and `interface.tsv`, the remake's own interface text | extracted once by `tools/extract_messages.py`; **edit these files directly** |
| `fonts/` | Liberation Sans Regular and Bold; Liberation Serif Regular and Bold Italic, for the intro's and ending's words | copied from the Liberation fonts 2.1.5 |

The pictures, sounds and tunes come from *Tower of the Sorcerer* 1.2 (Oz &
Kenichi), whose art is that of NAO's original *Mahō no Tō*; NAO allows fan
works based on the game.  The converters are kept in `tools/` as the
record of how each file was made, and to regenerate the files after a
converter fix (`tools/check_assets.py --game-dir` compares them with the
originals).

Liberation Sans and Serif are under the SIL Open Font License 1.1
(`fonts/LICENSE-LiberationSans.txt`, which covers the whole family:
Arimo, Tinos and Cousine).

The program's licence (GPL-3.0-or-later, `LICENSE`) does not cover the
pictures, sounds, tunes and original text in this folder; they are the
original game's, used as its author allows.

## Text

One line per entry, `<index>\t<text>`, with backslash, tab and newline
escaped as `\\`, `\t` and `\n`.

`messages.tsv` has a middle column, `<index>\t<speaker>\t<text>`: the id
of whoever says the line, empty for narration and status text.  The
dialogue window shows that speaker's name across its top and their map
sprite, animated as on the map, beside the words (a window takes the
speaker of its first line).  The ids are fixed in `src/messages.c` with
the sprite each one uses: `hero`, `thief`, `zeno`, `golden_knight`,
`skeleton_a`, `vampire`, `master` (the Great Magic Master), `old_man`,
`merchant`, `fairy` and `altar`.  `speakers.tsv` gives each id its shown
name, `<id>\t<name>`; a translation translates the names and keeps the
ids.  A line with only two columns has no speaker.

`interface.tsv` is the remake's own text around the game: menus, window
titles, key hints, the HUD, item names, Help > Controls and About, and
the console's log lines, and the words of the intro's and ending's
pictures (`picture.*`: the verses, "The End", "See you again!"; a
verse's lines are `\n` apart, its stanzas `\n\n`, and lines too wide
for the picture are squeezed to fit).  Lines are `<key>\t<text>` (`#` starts a
comment); the keys and the built-in English are in `src/itext.h`, which
the game falls back to for any key the file leaves out.  An entry with
`%d` or `%s` must keep them in the same order, or the game rejects it,
keeps the English and says so in the log.  `key.yes` / `key.no` are the
keys that answer a question (one letter or digit: `y`/`n`, `t`/`n` in
Polish, `c`/`k` in Vietnamese); the hints that name them are translated with them.

The index of a `messages.tsv` line is the original's, so it must not
change.  A translation is a copy of `en/` under another language code
(`pl/` is Polish, `vi/` Vietnamese, `ja/` Japanese, `ru/` Russian) with the text translated line by line and the speaker
column kept; `interface.tsv`'s `language.name` is the name the menu
shows.  Every folder of `text/` with a `messages.tsv` is a language:
Options > Language switches between them while the game runs, and
`options.ini` remembers the choice (`language = pl`).  Until a language is
chosen there, the game starts in the system's preferred language if
`text/` has it (Polish on a Polish system), else in English.  The text pieces
the game joins with numbers keep their leading and trailing spaces
where the joined sentence needs them.  `ui.tsv` 8 and 9 are the joiners the game
puts between such pieces itself: the word gap (" ", empty in Japanese)
and the sentence break (". ", "。" in Japanese).

The game ships no CJK font.  For Japanese it uses the system's (Noto
Sans CJK JP or another fontconfig finds on Linux, Yu Gothic / Meiryo /
MS Gothic on Windows, Hiragino on macOS) and draws all text with it
while Japanese is shown; a language whose name the fonts cannot draw
is left out of Options > Language.  Lines break between Japanese
characters (with kinsoku: no 。、」 or small kana at a line's start, no
「 at its end), and the ranking name shows the input method's text
while it is being composed.
