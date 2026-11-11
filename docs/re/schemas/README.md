# Event data schemas

These files are header-only schemas for the event-analysis pipeline.  Generated
rows come from the user's own binary and are written outside the repository
(by default under `$XDG_CACHE_HOME/tsw/`), not committed here.

| Schema | Purpose |
|---|---|
| `event_triggers.tsv` | trigger source, caller, handler and guard |
| `event_builder_ir.tsv` | executable handler/builder operations |
| `event_timelines.tsv` | resolved per-input command rows |
| `event_flags.tsv` | event/state flag map |
| `event_messages.tsv` | namespace-aware runtime message IDs |
| `event_writers.tsv` | whole-binary buffer-access inventory |

See [`../11-event-analysis-plan.md`](../11-event-analysis-plan.md) for the
workflow and acceptance criteria.
