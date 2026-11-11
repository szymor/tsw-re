# Refined game text

`text/` is for the **refined** message table, committed when the format is
stable.  The raw extraction is generated locally into `data/text/`
(gitignored) and is not committed.

## Files (planned)

| File | Purpose |
|---|---|
| `messages.tsv` | Refined, game-used text with runtime message IDs. |
| `mapping.tsv` | Runtime message ID ↔ DFM ordinal ↔ text key mapping. |

## Workflow

1. `docs/re/tools/extract_text.py` writes a raw extraction into
   `data/text/messages.raw.tsv`.
2. Refine it: remove non-game/UI strings, map runtime message IDs, and
   remove unused references.
3. Commit the refined `text/messages.tsv` only when the format is settled.
4. CMake copies `text/` into `data/text/` for the build.  `data/` is
   generated and gitignored; do not put raw source text there.
