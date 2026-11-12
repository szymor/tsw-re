# Refined game text

`text/` is for the **refined** message table, committed when the format is
stable.  The raw extraction is generated outside the repository, by default
into `$XDG_CACHE_HOME/tsw/` (or `~/.cache/tsw/`), and is not committed.

The **runtime** dialogue list is not here: `tools/extract_messages.py`
generates it from the user's own `TSW.exe` into `data/text/messages.tsv`
(ignored by Git, installed with the build), and the engine indexes it like
the original (`docs/re/18-messages.md`).  `text/` is reserved for a later
refined/localisation layer.

## Files (planned)

| File | Purpose |
|---|---|
| `messages.tsv` | Refined, game-used text with runtime message IDs. |
| `mapping.tsv` | Runtime message ID ↔ DFM ordinal ↔ text key mapping. |

## Workflow

1. `docs/re/tools/extract_text.py` writes a raw extraction outside the
   repository, by default to `$XDG_CACHE_HOME/tsw/messages.raw.tsv`.
2. Refine it: remove non-game/UI strings, map runtime message IDs, and
   remove unused references.
3. Commit the refined `text/messages.tsv` only when the format is settled.
4. CMake copies refined `text/` data into the build; raw source text is never
   copied and never committed.
