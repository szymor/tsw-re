# Build-time data tools

These scripts read the user's own Tower of the Sorcerer installation and write
generated data into `data/`.  `data/` is ignored by Git; conversion scripts are
committed.

| Script | Purpose |
|---|---|
| `convert_assets.py` | Convert the 640×400 `Data/*.bmp` panels and the `.oz` atlases (8-bit BMP containers) to PNG in `data/gfx/`, including the masked player sheet. |
| `extract_maps.py` | Copy a 6273-byte map block.  `--map-block` writes the pristine `data/maps/initial.bin`; `--save` writes a debug-only `data/maps/save-map.bin`. |

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
