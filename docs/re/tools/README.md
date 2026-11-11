# Analysis tools

These scripts read the user's own game files and write data locally.  They do
not contain game assets or full game data.  Keep generated files out of git.

| Script | Purpose |
|---|---|
| `read_save.py` | print state fields, validate checksums, print a floor, write a false-color PPM |
| `extract_initial_map.py` | rebuild the 6273-byte map block from a Ghidra decomp of `syokidata`, or copy it from a save |
| `extract_text.py` | extract the `TTSW10` DFM text strings from the user's `TSW.exe` |
| `extract_timelines.py` | inventory static 6-byte event append sites per floor (address-window; not control-flow-aware) |
| `scan_event_buffers.py` | whole-binary event count/command-buffer inventory (CFG dataflow, switch tables, register aliases) |
| `scan_event_triggers.py` | decode event switch tables, call sites and first-pass trigger inventory |
| `scan_event_abi.py` | inventory call-site register setup for event entry points |
| `event_snapshot.py` | deterministic oracle snapshot model and save-file loader |
| `lift_event_handler.py` | CFG-aware first-pass append-site lift for one handler |
| `build_event_ir.py` | merge lifter TSVs into a builder-IR JSON bundle |
| `run_synthetic_oracle.py` | execute a builder-IR bundle with the reference VM |
| `native_event_oracle.py` | experimental Unicorn harness for native handler calls (VCL stubs; partial) |
| `event_vm.py` | reference model of the 6-byte timeline VM |
| `test_event_vm.py` | unit tests for the reference VM |
| `test_scan_event_buffers.py` | unit tests for the buffer-access scanner |
| `test_scan_event_triggers.py` | unit tests for the trigger extractor |
| `test_event_snapshot.py` | unit tests for the snapshot model |
| `test_scan_event_abi.py` | unit tests for the ABI call-site inventory |
| `test_lift_event_handler.py` | unit tests for the append-site lifter |
| `test_build_event_ir.py` | unit tests for builder-IR merging and the synthetic oracle |
| `test_native_event_oracle.py` | optional Unicorn native-harness self-test (skipped without unicorn) |

The event data schemas are header-only files under `docs/re/schemas/`.
Generated tables and raw dumps stay outside the repository.

Examples:

Run from the repository root:

```sh
python3 docs/re/tools/read_save.py "$GAME/Savedat/save1.dat" --state
python3 docs/re/tools/read_save.py "$GAME/Savedat/save1.dat" --floor 1 --ppm /tmp/floor1.ppm
python3 docs/re/tools/extract_initial_map.py --decomp /tmp/opencode/decomp/00455cb8_FUN_00455cb8.c -o /tmp/initial_map.bin
python3 docs/re/tools/extract_text.py "$GAME/TSW.exe"   # writes outside the repo
python3 docs/re/tools/scan_event_buffers.py "$GAME/TSW.exe" --summary
python3 docs/re/tools/scan_event_triggers.py "$GAME/TSW.exe" --summary
python3 docs/re/tools/scan_event_abi.py "$GAME/TSW.exe" --summary
python3 docs/re/tools/event_snapshot.py "$GAME/Savedat/save1.dat" --floor 3 -o /tmp/snapshot.json
python3 docs/re/tools/lift_event_handler.py "$GAME/TSW.exe" --method floor10 --summary
python3 docs/re/tools/lift_event_handler.py "$GAME/TSW.exe" --method floor10 --dir /tmp/floor10-ir
python3 docs/re/tools/build_event_ir.py /tmp/floor10-ir
python3 docs/re/tools/run_synthetic_oracle.py /tmp/floor10-ir/builder_ir.json /tmp/snapshot.json --entry-block 0x46cbc7 -o /tmp/floor10-trace.json
/tmp/opencode/venv/bin/python docs/re/tools/native_event_oracle.py --self-test
python3 docs/re/tools/test_event_vm.py
```

`extract_text.py` is a heuristic DFM scanner: it finds `0x06 <len> <text>`
records.  The full message list is a Delphi string list, so some entries may
need a more structured parser later.
