#!/usr/bin/env python3
"""Report static-CFG coverage for event handlers from golden vectors.

The static side is the CFG and append-site inventory of the pinned binary
(``scan_event_buffers``); the dynamic side is the ordered execution trace
stored in the golden vectors (``executed_runs``).  Coverage is reported per
handler region:

* blocks: every in-region basic block that at least one witness executed;
* edges: every static successor edge of an in-region block that a witness
  took (including edges that leave the region);
* append sites: every command-opcode store in the region that a witness
  executed.

This is the P4/P6 coverage gate.  It does not use recorded gameplay: each
witness is a synthetic snapshot from ``export_event_vectors.py``.

Usage:
    python3 docs/re/tools/check_event_coverage.py
    python3 docs/re/tools/check_event_coverage.py --details
    python3 docs/re/tools/check_event_coverage.py --require ichicheck
    python3 docs/re/tools/check_event_coverage.py --json /tmp/coverage.json
"""

from __future__ import annotations

import argparse
import bisect
import json
import os
import re
import sys
from pathlib import Path

import scan_event_buffers as buffers
import scan_event_triggers as triggers

EXPECTED_SHA256 = buffers.EXPECTED_SHA256

# Handlers that P4/P6 must cover.  The ichicheck body is one function with
# interior floor-case entries in EVENT_HANDLERS, so its region ends at the
# next real method (dooropen), not at floor15.
TRACKED = [
    "ichicheck", "mevent", "handan",
    "roujin", "syounin", "touzoku", "yousei", "moncheck",
    "taisen2", "madoushi1", "madoushi2", "banpaiawork",
    "openning2", "openning31", "openning9",
    "zenolivework", "madoushilivework", "doorstackin",
    "Button1Click", "Timer1Timer", "gameover",
    # Item use (overlay Enter) and the Orb of Flying's panel.
    "img4work", "img5work", "img6work", "img8work", "img9work",
    "img10work", "img11work", "img12work", "img13work", "img15work",
    "img16work", "img17work", "Button37Click", "Button38Click",
    "matokwork", "yukiwork", "mmmes", "toroad2", "yousya", "obudameiji",
    "Button8Click", "Button9Click", "Button10Click",
]

REGION_OVERRIDES = {
    "ichicheck": (0x46CB1C, 0x4770E0),
}


def method_regions(methods: list[tuple[int, str]]) -> dict[str, tuple[int, int]]:
    entries = sorted(set(methods) | set(buffers.EVENT_HANDLERS))
    regions: dict[str, tuple[int, int]] = {}
    for name in TRACKED:
        hits = [(addr, label) for addr, label in entries if label == name]
        if not hits:
            continue
        start = hits[0][0]
        index = entries.index(hits[0])
        end = entries[index + 1][0] if index + 1 < len(entries) else 0x488E94
        regions[name] = (start, end)
    regions.update(REGION_OVERRIDES)
    return regions


def build_successors(insns, blocks, addr_to_block, table_edges):
    """Return {block_index: [successor_block_index, ...]}."""
    successors: dict[int, list[int]] = {}
    for bi, (bs, be) in enumerate(blocks):
        last_addr, last = insns[be]
        op = last.split(" ", 1)[0]
        out: list[int] = []
        if op in buffers.COND_BRANCHES:
            target = buffers.direct_target(last)
            if target in addr_to_block:
                out.append(addr_to_block[target])
            if bi + 1 < len(blocks):
                out.append(bi + 1)
        elif op == "jmp":
            target = buffers.direct_target(last)
            if target in addr_to_block:
                out.append(addr_to_block[target])
            elif last_addr in table_edges:
                for switch_target in table_edges[last_addr]:
                    if switch_target in addr_to_block:
                        out.append(addr_to_block[switch_target])
        elif op in ("ret", "retn", "retf", "iretd"):
            pass
        elif bi + 1 < len(blocks):
            out.append(bi + 1)
        successors[bi] = list(dict.fromkeys(out))
    return successors


def block_at(blocks, insns, addr, starts=None):
    """Return the block containing ``addr`` (also mid-block), or None."""
    if starts is None:
        starts = [insns[bs][0] for bs, _ in blocks]
    index = bisect.bisect_right(starts, addr) - 1
    if index < 0:
        return None
    _bs, be = blocks[index]
    if insns[be][0] < addr:
        return None
    return index


def runs_to_blocks(runs, blocks, insns, addr_to_block):
    """Map ordered address runs to an ordered list of distinct block indices.

    A run may start mid-block (a call returns into the middle of its caller),
    so the exact leader map is only a fast path; fall back to the containing
    block.  Consecutive blocks inside a run are fall-through transitions; a
    new run is a control transfer.  Call/return boundaries also start new
    runs, but their source/target pair is not a static CFG successor and is
    ignored by the edge pass.
    """
    starts = [insns[bs][0] for bs, _ in blocks]
    ordered: list[int] = []
    for start, end in runs:
        bi = addr_to_block.get(start)
        if bi is None:
            bi = block_at(blocks, insns, start, starts)
        while bi is not None:
            block_start = insns[blocks[bi][0]][0]
            if block_start >= end:
                break
            if not ordered or ordered[-1] != bi:
                ordered.append(bi)
            bi += 1
            if bi >= len(blocks):
                break
            if insns[blocks[bi][0]][0] >= end:
                break
    return ordered


def ordered_transitions(ordered: list[int]) -> list[tuple[int, int]]:
    return [(a, b) for a, b in zip(ordered, ordered[1:]) if a != b]


def region_blocks(blocks, insns, start, end, data_ranges, successors):
    """Reachable, in-region, non-data blocks from the region entry.

    Control-flow successors are followed directly.  Direct calls whose target
    also lies inside the region are followed too: the method table has no
    separate entries for inlined helpers, and those helpers are part of the
    handler's coverage closure.  Call edges are not CFG edges and are not
    counted by the edge pass.
    """
    region = set()
    for bi, (bs, be) in enumerate(blocks):
        block_start = insns[bs][0]
        if not (start <= block_start < end):
            continue
        if any(lo <= block_start < hi for lo, hi in data_ranges):
            continue
        region.add(bi)
    entry = block_at(blocks, insns, start)
    if entry is None or entry not in region:
        return []
    seen = {entry}
    queue = [entry]
    while queue:
        bi = queue.pop()
        targets = list(successors.get(bi, []))
        bs, be = blocks[bi]
        for _addr, text in insns[bs:be + 1]:
            match = re.match(r"^call\s+0x([0-9a-f]+)$", text)
            if match:
                callee = block_at(blocks, insns, int(match.group(1), 16))
                if callee is not None:
                    targets.append(callee)
        for target in targets:
            if target in region and target not in seen:
                seen.add(target)
                queue.append(target)
    return sorted(seen)


def block_address(blocks, insns, bi):
    return insns[blocks[bi][0]][0]


def append_sites(accesses):
    return sorted({
        int(access["address"]) for access in accesses
        if access["target"] == "cmd_opcode"
        and access["access"] in ("write", "read_write")
    })


def load_vectors(directory: Path):
    cases = []
    if not directory.is_dir():
        return cases
    for path in sorted(directory.glob("*.json")):
        if path.name == "manifest.json":
            continue
        vector = json.loads(path.read_text(encoding="utf-8"))
        if "executed_runs" not in vector and "executed" not in vector:
            continue
        cases.append(vector)
    return cases


def case_coverage(vector, blocks, insns, addr_to_block, successors):
    covered_blocks: set[int] = set()
    covered_edges: set[tuple[int, int]] = set()
    if "executed_runs" in vector:
        ordered = runs_to_blocks(vector["executed_runs"], blocks, insns,
                                 addr_to_block)
    else:
        # Old vectors only have a sorted address set.  It is enough for block
        # coverage, but not for edge coverage; fail closed on edges.
        starts = [insns[bs][0] for bs, _ in blocks]
        ordered = []
        for address in vector.get("executed", []):
            bi = addr_to_block.get(address)
            if bi is None:
                bi = block_at(blocks, insns, address, starts)
            if bi is not None and (not ordered or ordered[-1] != bi):
                ordered.append(bi)
        for bi in ordered:
            covered_blocks.add(bi)
        return covered_blocks, covered_edges
    for bi in ordered:
        covered_blocks.add(bi)
    for src, dst in ordered_transitions(ordered):
        if dst in successors.get(src, []):
            covered_edges.add((src, dst))
    # A block that ends in a call falls through to the next block only after
    # the callee returns, so the callee's blocks sit between the two in the
    # trace.  A run starting exactly at the next block's leader (the return
    # address) after the calling block executed is that edge.
    run_starts = {start for start, _end in vector["executed_runs"]}
    for src in covered_blocks:
        _bs, be = blocks[src]
        if not insns[be][1].startswith("call"):
            continue
        for dst in successors.get(src, []):
            if dst == src + 1 and insns[blocks[dst][0]][0] in run_starts:
                covered_edges.add((src, dst))
    return covered_blocks, covered_edges


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", type=Path,
                    default=Path.home() / (
                        ".wine/drive_c/Program Files (x86)/Tower of the "
                        "Sorcerer/TSW.exe"))
    ap.add_argument("--vectors", type=Path, default=None)
    ap.add_argument("--require", action="append", default=[],
                    help="fail if this region is not fully covered (repeatable)")
    ap.add_argument("--details", action="store_true",
                    help="list uncovered blocks, edges and append sites")
    ap.add_argument("--json", type=Path, help="write the full report as JSON")
    ap.add_argument("--allow-other", action="store_true")
    args = ap.parse_args()

    vectors_dir = args.vectors or (
        Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache"))
        / "tsw" / "vectors")
    if not args.exe.exists():
        print(f"error: {args.exe} does not exist", file=sys.stderr)
        return 2
    digest = buffers.sha256(args.exe)
    if digest != EXPECTED_SHA256 and not args.allow_other:
        print(f"error: binary hash {digest} does not match {EXPECTED_SHA256}",
              file=sys.stderr)
        return 3
    cases = load_vectors(vectors_dir)
    if not cases:
        print(f"error: no golden vectors in {vectors_dir}", file=sys.stderr)
        return 4

    true_methods = buffers.load_methods(
        Path(__file__).resolve().parents[1] / "method-table.tsv")
    methods = triggers.method_labels(true_methods)
    handler_addrs = {addr for addr, _ in buffers.EVENT_HANDLERS}
    entry_addrs = ({addr for addr, _ in methods} | handler_addrs)
    insns = buffers.disassemble(args.exe, entry_addrs)
    table_edges, data_ranges = buffers.recover_indirect_edges(args.exe, insns)
    entry_addrs |= {t for targets in table_edges.values() for t in targets}
    insns = buffers.disassemble(args.exe, entry_addrs)
    table_edges, data_ranges = buffers.recover_indirect_edges(args.exe, insns)
    data_ranges = list(data_ranges) + list(buffers.KNOWN_DATA_RANGES)
    blocks, addr_to_block, _ = buffers.build_blocks(insns, entry_addrs)
    successors = build_successors(insns, blocks, addr_to_block, table_edges)

    accesses, _base, _unresolved = buffers.scan(
        insns, methods, true_methods, handler_addrs, table_edges, data_ranges)
    sites = append_sites(accesses)

    covered_blocks: set[int] = set()
    covered_edges: set[tuple[int, int]] = set()
    covered_sites: set[int] = set()
    for vector in cases:
        c_blocks, c_edges = case_coverage(
            vector, blocks, insns, addr_to_block, successors)
        covered_blocks |= c_blocks
        covered_edges |= c_edges
        executed = set(vector.get("executed", []))
        covered_sites |= {site for site in sites if site in executed}
        # The drain's calls (stackwork, Button1Click), each its own trace:
        # code the VM reaches (openning9, the splices) counts too.
        drain = vector.get("drain") or {}
        for segment in drain.get("executed_segments", []):
            if not segment:
                continue
            c_blocks, c_edges = case_coverage(
                {"executed_runs": segment}, blocks, insns, addr_to_block,
                successors)
            covered_blocks |= c_blocks
            covered_edges |= c_edges
            seg_addrs = {a for start, end in segment
                         for a in range(start, end)}
            covered_sites |= {site for site in sites if site in seg_addrs}

    regions = method_regions(methods)
    if args.require:
        missing = [name for name in args.require if name not in regions]
        if missing:
            print(f"error: unknown region(s): {', '.join(missing)}",
                  file=sys.stderr)
            return 5

    report = {
        "schema": "tsw-event-coverage/1",
        "binary_sha256": digest,
        "vectors": str(vectors_dir),
        "cases": len(cases),
        "regions": {},
    }
    print(f"binary:  {digest}")
    print(f"vectors: {vectors_dir} ({len(cases)} cases)")
    print(f"{'handler':18} {'blocks':>13} {'edges':>13} {'appends':>13}")
    required_failed = []
    for name, (start, end) in sorted(regions.items()):
        region = set(region_blocks(
            blocks, insns, start, end, data_ranges, successors))
        region_blocks_cov = region & covered_blocks
        region_edges = {
            (src, dst) for src in region for dst in successors.get(src, [])
        }
        region_edges_cov = region_edges & covered_edges
        region_sites = {site for site in sites if start <= site < end}
        region_sites_cov = region_sites & covered_sites
        entry = {
            "start": f"0x{start:08x}",
            "end": f"0x{end:08x}",
            "blocks": {"covered": len(region_blocks_cov),
                       "total": len(region)},
            "edges": {"covered": len(region_edges_cov),
                      "total": len(region_edges)},
            "append_sites": {"covered": len(region_sites_cov),
                             "total": len(region_sites)},
        }
        complete = (len(region_blocks_cov) == len(region)
                    and len(region_edges_cov) == len(region_edges)
                    and len(region_sites_cov) == len(region_sites))
        # An empty region is not "complete"; it means the region is missing.
        entry["complete"] = bool(complete and region)
        if name in args.require and not entry["complete"]:
            required_failed.append(name)
        if args.details and not entry["complete"]:
            missing_blocks = sorted(region - region_blocks_cov)
            missing_edges = sorted(region_edges - region_edges_cov)
            missing_sites = sorted(region_sites - region_sites_cov)
            entry["uncovered"] = {
                "blocks": [f"0x{block_address(blocks, insns, bi):08x}"
                           for bi in missing_blocks],
                "edges": [
                    [f"0x{block_address(blocks, insns, a):08x}",
                     f"0x{block_address(blocks, insns, b):08x}"]
                    for a, b in missing_edges
                ],
                "append_sites": [f"0x{site:08x}" for site in missing_sites],
            }
        report["regions"][name] = entry
        print(f"{name:18} "
              f"{len(region_blocks_cov):5d}/{len(region):<7d} "
              f"{len(region_edges_cov):5d}/{len(region_edges):<7d} "
              f"{len(region_sites_cov):5d}/{len(region_sites):<7d}")
        if args.details and not entry["complete"]:
            for label, values in entry["uncovered"].items():
                if values:
                    print(f"  uncovered {label}: "
                          + " ".join(
                              "->".join(v) if isinstance(v, list) else v
                              for v in values))

    if args.json:
        args.json.write_text(json.dumps(report, indent=2) + "\n",
                             encoding="utf-8")
        print(f"wrote {args.json}")
    if required_failed:
        print("coverage gate failed: " + ", ".join(required_failed),
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
