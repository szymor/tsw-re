# Analysis tools

These scripts read the user's own game files and write data locally.  They do
not contain game assets or full game data.  Keep generated files out of git.

| Script | Purpose |
|---|---|
| `read_save.py` | print state fields, validate checksums, print a floor, write a false-color PPM |
| `extract_initial_map.py` | rebuild the 6273-byte map block from a Ghidra decomp of `syokidata`, or copy it from a save |
| `read_dfm.py` | parse the `TTSW10` binary DFM (`TPF0`) and its published-field table (Delphi 2 VMT): form offset → component → string lists (`--fields`, `--lists`, `--list-at 0x44c`) |
| `extract_text.py` | extract the `TTSW10` DFM text strings from the user's `TSW.exe` |
| `extract_timelines.py` | inventory static 6-byte event append sites per floor (address-window; not control-flow-aware) |
| `scan_event_buffers.py` | whole-binary event count/command-buffer inventory (CFG dataflow, switch tables, register aliases) |
| `scan_event_triggers.py` | decode event switch tables, call sites and first-pass trigger inventory |
| `scan_event_abi.py` | inventory call-site register setup for event entry points |
| `event_snapshot.py` | deterministic oracle snapshot model and save-file loader |
| `lift_event_handler.py` | CFG-aware first-pass append-site lift for one handler |
| `build_event_ir.py` | merge lifter TSVs into a builder-IR JSON bundle |
| `run_synthetic_oracle.py` | execute a builder-IR bundle with the reference VM |
| `compose_event_timeline.py` | trace-free static timeline composer with callee inlining and path/loop/effect contracts |
| `native_event_oracle.py` | Unicorn harness for native handler calls with explicit presentation-helper contracts |
| `export_event_vectors.py` | run the witness matrix through the native oracle and write golden vectors (`.json` + `.vec`) to `$XDG_CACHE_HOME/tsw/vectors` |
| `check_event_coverage.py` | report per-handler block/edge/append-site coverage from the static CFG and the vectors' ordered execution runs; `--require` is the P4/P6 gate |
| `run_execution_oracle.py` | P3b execution oracle: builder + original `stackwork` drain with state/map write hooks; suspends on op `0x0a` and confirms every `zyouout` window through `Button1Click` (`--complete-dialogs`); strict by default, optional tainted `--permissive-vcl` |
| `run_native_oracle.py` | run the four main pilot handlers natively and write deterministic JSON traces (save or canonical snapshot) |
| `event_vm.py` | reference model of the 6-byte timeline VM |
| `test_event_vm.py` | unit tests for the reference VM |
| `test_scan_event_buffers.py` | unit tests for the buffer-access scanner |
| `test_scan_event_triggers.py` | unit tests for the trigger extractor |
| `test_event_snapshot.py` | unit tests for the snapshot model |
| `test_scan_event_abi.py` | unit tests for the ABI call-site inventory |
| `test_lift_event_handler.py` | unit tests for the append-site lifter |
| `test_build_event_ir.py` | unit tests for builder-IR merging and the synthetic oracle |
| `test_native_event_oracle.py` | optional Unicorn native-harness self-test (skipped without unicorn) |
| `test_run_execution_oracle.py` | optional execution-oracle tests: `roujin` drain, strict fail-closed, floor-3 opening end to end (skipped without unicorn/TSW.exe; `$TSW_EXE` overrides the path) |
| `test_compose_event_timeline.py` | static composer integration tests for the pilot timelines |
| `test_check_event_coverage.py` | unit tests for the coverage mapping (runs → blocks/edges, region filters, append sites) |

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
python3 docs/re/tools/compose_event_timeline.py /tmp/floor10-ir/builder_ir.json --callee-dir /tmp/callees --snapshot /tmp/snapshot.json --entry-block 0x46cbc7 -o /tmp/static-timeline.json
/tmp/opencode/venv/bin/python docs/re/tools/run_native_oracle.py --all -d /tmp/native-traces
/tmp/opencode/venv/bin/python docs/re/tools/export_event_vectors.py   # -> ~/.cache/tsw/vectors
python3 docs/re/tools/check_event_coverage.py --details
python3 docs/re/tools/check_event_coverage.py --require ichicheck
/tmp/opencode/venv/bin/python docs/re/tools/run_execution_oracle.py --case handan_floor3 --complete-dialogs --max-steps 4000 --summary
/tmp/opencode/venv/bin/python docs/re/tools/run_native_oracle.py --snapshot /tmp/snapshot.json --case roujin
python3 docs/re/tools/test_event_vm.py
```

`extract_text.py` is a heuristic DFM scanner: it finds `0x06 <len> <text>`
records.  The structured dialogue list (`ListBox2`) is extracted by
`tools/extract_messages.py` through `read_dfm.py` (`18-messages.md`).
