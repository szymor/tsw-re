# tsw-re

Reverse-engineering notes and (later) a C + SDL3 re-implementation of
**Tower of the Sorcerer v1.2**.

Current status: **analysis only**.  The re-implementation has not started;
it waits for approval of the plan in `docs/re/08-implementation-plan.md`.

Start here:

* [`docs/re/README.md`](docs/re/README.md) — overview and index
* [`docs/re/02-map-format.md`](docs/re/02-map-format.md) — map/save layout
* [`docs/re/03-state-and-save.md`](docs/re/03-state-and-save.md) — state fields and saves
* [`docs/re/04-game-logic.md`](docs/re/04-game-logic.md) — input, movement, items, battle
* [`docs/re/06-data-tables.md`](docs/re/06-data-tables.md) — monster/item tables
* [`docs/re/07-method-table.md`](docs/re/07-method-table.md) — function map
* [`docs/re/08-implementation-plan.md`](docs/re/08-implementation-plan.md) — proposed C/SDL3 plan

No copyrighted game assets or full game data are stored in this repository.
The tools in `docs/re/tools/` read the user's own installation and produce
data locally.
