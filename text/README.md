# Original game text

This directory holds the original Tower of the Sorcerer text as committed
source.  It is an explicit project-owner decision to keep the original
wording in the repository for refinement; distribution rights remain the
project owner's responsibility.

## Files

| File | Purpose |
|---|---|
| `messages.tsv` | Current working text extraction (first pass; includes some UI/font strings). |
| `messages.raw.tsv` | Optional: untouched extractor output, if we want to preserve a raw baseline. |
| `mapping.tsv` | Optional: runtime message ID ↔ DFM ordinal ↔ text key mapping. |

## Workflow

1. `docs/re/tools/extract_text.py` produces a raw extraction from the user's
   `TSW.exe`.
2. Refine it into `messages.tsv`: remove non-game/UI strings, map runtime
   message IDs, and remove unused references.
3. CMake copies `text/` into `data/text/` for the build.  `data/` is
   generated and gitignored; do not put source text there.

The first pass currently in `messages.tsv` is a baseline, not the final
message set.
