# Data tools

These scripts read an original Tower of the Sorcerer 1.2 installation and
made the committed `data/` (`data/README.md`).  A build does not need them;
they are the record of how each file was made, and regenerate the files
after a converter fix (review the result with `git diff`).

| Script | Purpose |
|---|---|
| `convert_assets.py` | Convert the 640×400 `Data/*.bmp` panels and the `.oz` atlases (8-bit BMP containers) to PNG in `data/gfx/`, including the masked player sheet (three poses per facing) and the original TSW.exe window icon; the ending's `Data2/End*.bmp` pictures and the pictures embedded in the main form (`Image21`, `Image33`, `Image34`). |
| `extract_maps.py` | Copy a 6273-byte map block.  `--map-block` writes the pristine `data/maps/initial.bin`; `--save` writes a debug-only `data/maps/save-map.bin`. |
| `extract_messages.py` | Extract the original dialogue list (`TTSW10.ListBox2`, form field `+0x44c`) from `TSW.exe`, with the default texts of form components the engine shows (`ui.tsv`).  It made the committed `data/text/en/`; it now writes to `~/.cache/tsw/text/` by default, for comparison, so it never overwrites the edited text. |
| `make_clean_titles.py` | Erase the credit lettering from the title pictures into `data/gfx/title05_clean.png` and `title05R_clean.png`; the game writes the credits over them, the remake's among them (`src/render.c`).  Each block of the lettering is refilled from wherever the surrounding stone texture matches it exactly, and the script fails if any lettering is left.  Needs Pillow and NumPy. |
| `build_sdl3_headless.sh` | Build SDL3, SDL3_image and SDL3_ttf from source into a private prefix (default `~/opt/sdl3`) for machines without SDL3 packages (CI, containers, sandboxed VMs).  Headless by default (`--desktop` keeps window/audio backends); enough for `ctest` with `SDL_VIDEODRIVER=dummy` and `--screenshot`. |

Examples:

```sh
GAME="$HOME/.wine/drive_c/Program Files (x86)/Tower of the Sorcerer"

python3 tools/convert_assets.py --game-dir "$GAME"
python3 tools/extract_maps.py --map-block /path/to/initial_map_block.bin
# debug only: --save "$GAME/Savedat/save1.dat" -o data/maps/save-map.bin

./build/tsw-re
```

`Chtable.oz` is an 8-bit indexed BMP container with a `.oz` extension;
`Chtableb.oz` is the matching sprite bitmask.  Both are converted directly to
PNG.  Raw text extraction lives in `docs/re/tools/extract_text.py` and writes
outside the repository.

* `check_assets.py` — check the generated `data/` tree (files, sizes, a
  lossless comparison with the originals via `--game-dir`, the sprite mask,
  sounds and tunes).
