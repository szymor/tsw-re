# Map format

## Save-file map block

A save file has two major blocks:

```
offset 0      : map block      6273 bytes (0x1881)
offset 6273   : state block     684 bytes (0x02ac)
total         : 6957 bytes
```

The map block is:

```
2-byte header          = 00 01
51 map records         = 121 bytes each (11 × 11 tiles)
2-byte gap after each map except the last = 50 × 2 = 100 bytes
```

Total: `2 + 51*121 + 50*2 = 6273`.

The first record (index 0) is a dummy/placeholder map.  Game floors are
indices 1..50, so **floor N is map index N**.  The runtime address of floor
`N` is:

```
0x4b8936 + N * 0x7b
```

because each runtime record is `0x7b = 123` bytes: the 121-byte map preceded
by a 2-byte stair table.  Floor 1 therefore starts at `0x4b89b1`;
the two stair bytes are at `0x4b89af` and `0x4b89b0`.

### The 2-byte stair table

The two bytes before each floor map are **stair arrival positions**, encoded
as tile indices `0..120`:

```
byte 0 (0x4b8934 + floor*0x7b): arrival position when entering floor N
                                from below (going up)
byte 1 (0x4b8935 + floor*0x7b): arrival position when entering floor N
                                from above (going down)
```

`handan` converts the byte to `(x, y)` as `x = pos % 11`, `y = pos / 11`
after the floor transition.  Examples from the initial map:

| Floor | Bytes | Up arrival | Down arrival |
|---:|---|---|---|
| 1 | `01 01` | (1,0) | (1,0) |
| 2 | `0b 63` | (0,1) | (0,9) |
| 3 | `6f 77` | (1,10) | (9,10) |

The two bytes are preserved by save/load.  The dummy map index 0 has no stair
pair: the bytes at `0x4b8934..0x4b8935` are the block header `00 01`.  The
first real stair pair is the one immediately before floor 1 at
`0x4b89af..0x4b89b0` (`01 01`).

## Initial map

The initial map is not a separate file.  It is embedded as a long sequence of
byte stores in `syokidata` (`0x455cb8`).  The sequence writes exactly:

```
0x4b8934 = 0x00
0x4b8935 = 0x01
0x4b8936 .. 0x4ba1b4 = 6271 map bytes
```

The extracted sequence matches the new-game runtime memory exactly.  The
extraction script `tools/extract_initial_map.py` parses the byte stores from a
Ghidra decompilation of `syokidata`; it does not contain the map itself.

## Tile encoding

The map byte is `sprite index + 1`.  The renderer draws sprite `value - 1`
from the tile atlas.  Known values:

| Raw | Sprite | Meaning |
|---:|---:|---|
| `0x01` | 0 | yellow door (consumes yellow key) |
| `0x02` | 1 | blue door (consumes blue key) |
| `0x03` | 2 | red door (consumes red key) |
| `0x04` | 3 | open door / door frame |
| `0x05` | 4 | open door / door frame |
| `0x06` | 5 | floor |
| `0x07` | 6 | gray brick wall |
| `0x08` | 7 | brown brick wall |
| `0x09` | 8 | brown brick wall variant |
| `0x0a` | 9 | brown brick wall variant |
| `0x0b` | 10 | stairs up (increments floor) |
| `0x0c` | 11 | stairs down (decrements floor) |
| `0x0d` | 12 | **lava** (impassable until the Snow Crystal is used) |
| `0x0e` | 13 | ice/sparkle floor (likely the Snow Crystal's frozen-lava state) |
| `0x0f`–`0x11` | 14–16 | decorative / special floor |
| `0x13`, `0x15`, `0x19`, `0x1b` | 18, 20, 24, 26 | event / NPC markers |
| `0x1d` | 28 | yellow key |
| `0x1e` | 29 | red key |
| `0x1f` | 30 | blue key |
| `0x20` | 31 | life potion (small) |
| `0x21` | 32 | life potion (large) |
| `0x22` | 33 | red crystal: +attack |
| `0x23` | 34 | blue crystal: +defense |
| `0x25`–`0x32` | 36–49 | 14 special/quest item pickups |
| `0x33`–`0x37` | 50–54 | five swords |
| `0x38`–`0x3c` | 55–59 | five shields |
| `0x3d`–`0x5a` | 60–89 | normal monsters (table indices 0..14) |
| `0x5b`–`0x5c` | 90–91 | floor-49/50 boss events |
| `0x5d` | 92 | floor event trigger (11 placements) |
| `0x61`–`0x69` | 96–104 | special entity/event markers |
| `0x73`–`0x7b` | 114–122 | special entity/event markers |
| `0x85`–`0x9d` | 132–156 | special entity/event markers |

The exact event mapping for `0x61..0x9d` is still being decoded; these values
are handled by the `else` branch of `handan` and by the per-floor event table,
not by the normal monster encounter path.

## Rendering

`mhyouji` (`0x442c38`) draws the current floor:

```
for i = 1..121:
    mapIndex = renderOrder[i]          # table base 0x489b1f, 1-based
    raw      = mapBase[floor*0x7b + mapIndex]
    sprite   = raw - 1
    x        = (mapIndex * tileW) % (tileW * 11)
    y        = ((mapIndex * tileW) / (tileW * 11)) * tileH
    draw sprite at (x, y) on the map image
```

The original computes the pixel position from the **source map index** stored
in the render-order table, not from the loop counter.  If the table is the
identity permutation (0..120 row-major), this is equivalent to using `i-1`.

`tileW`/`tileH` come from the form and are normally 32.  A second layer is
drawn with an offset from `DAT_0048c554` for door/animation frames.

## Map order table

The 121-byte table at `0x489b20` maps draw-loop index to map index.  The code
accesses it as `0x489b1f + i` for `i = 1..121`, so `0x489b1f` is a
one-before-start base and the first real byte is `0x489b20`.  It is the
row-major order in the current build; the exact table is runtime data and can
be read from the process or reconstructed from the `syokidata` data.
