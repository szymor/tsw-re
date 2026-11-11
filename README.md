# tsw-re

Reverse-engineering notes and a planned C + SDL3 re-implementation of
**Tower of the Sorcerer v1.2**.

Current status: **design draft**.  No game code has been written yet; the
implementation starts after design approval.

Start here:

* [`docs/design/00-design.md`](docs/design/00-design.md) — PoC design:
  640×480 layout, asset pipeline, text policy, architecture and milestones
* [`docs/re/README.md`](docs/re/README.md) — reverse-engineering overview
* [`docs/re/02-map-format.md`](docs/re/02-map-format.md) — map/save layout
* [`docs/re/03-state-and-save.md`](docs/re/03-state-and-save.md) — state fields and saves
* [`docs/re/04-game-logic.md`](docs/re/04-game-logic.md) — input, movement, items, battle
* [`docs/re/06-data-tables.md`](docs/re/06-data-tables.md) — monster/item tables
* [`docs/re/07-method-table.md`](docs/re/07-method-table.md) — function map
* [`docs/re/08-implementation-plan.md`](docs/re/08-implementation-plan.md) — implementation phases
* [`docs/re/10-events-and-items.md`](docs/re/10-events-and-items.md) — events, timelines, items

Converted game assets are not stored in this repository; the final build
includes them and the conversion scripts live in `docs/re/tools/`.  The
original game text is stored as-is under `text/` by project decision.
