# Open questions

Almost every question raised during the analysis has been answered; the
answers are in the reference notes (`02`..`07`, `10`, `12`, `13`, `15`,
`18`..`20`) and every ported handler is checked against the original
(`16-event-pilot.md`).  The list of answered questions is in the Git
history of this file.  What is still open:

1. **State ints `0x2a0` / `0x2a4`** (`0x4b8928` / `0x4b892c`, indices
   `0xa8`/`0xa9`).  No writer has been found in the event code; they may be
   save metadata.  The remake keeps and saves them as they are.
2. **`option.dat` bytes 6 and 9.**  `option.dat` is 10 bytes; bytes 0..2,
   4, 5, 7 and 8 are mapped to menu handlers (`03-state-and-save.md`), but
   bytes 6 and 9 have no known writer except the reset to 0.  The remake
   keeps its own `options.ini` and does not read `option.dat`.
3. **Option controls at form `+0x3c8` / `+0x1c8`.**  `OptionSave1Click`
   reads those form offsets, but `disp` uses the same offsets for the life
   and weapon-name images.  Either the handler belongs to another form or
   the mapping is indirect.  It does not affect the remake.
