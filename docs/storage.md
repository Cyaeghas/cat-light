# Storage Roadmap

`cat-light` currently has a lightweight JSONL history backend plus an optional SQLite backend behind the same storage interface.

## Current Files

Direct event intake:

```text
<data-root>/events.jsonl
```

Synced history:

```text
<data-root>/history/events.jsonl
<data-root>/history/sessions.json
```

`events.jsonl` receives manual CLI events, hook events, and `POST /event`. `cat-light sync` reads that file plus passive Codex and Claude JSONL logs, dedupes events, and appends envelopes to `history/events.jsonl`.

`history-summary` counts events directly, but token totals come from merged sessions instead of summing every usage snapshot. This avoids double-counting providers that emit cumulative token usage.

Tool and command aggregates currently count events and distinct sessions. They intentionally do not attribute cumulative session tokens to a tool, because provider logs usually report total usage snapshots rather than per-tool token deltas.

History readers support UTC time windows with `--since`, `--until`, and `--days`. Date-only `--until` values include the whole UTC day. Daily trend buckets attribute event counts by event timestamp and token totals by each merged session's latest activity day, which preserves cumulative-token semantics.

## Commands

```powershell
cat-light sync
cat-light sync-json
cat-light history
cat-light history-json
cat-light history-summary
cat-light history-summary-json
cat-light history-trends-json
```

Local HTTP endpoints:

```text
GET  /history?days=30
GET  /history-summary?since=2026-06-04&until=2026-06-04
GET  /history-trends?provider=codex&days=7
POST /sync
```

Fixture test:

```powershell
.\scripts\test-fixtures.ps1
```

## Optional SQLite

SQLite is enabled at build time:

```powershell
cmake --preset msvc-sqlite
cmake --build --preset msvc-sqlite
```

When compiled in, `--storage sqlite` writes to:

```text
<data-root>/history/cat-light.sqlite
```

Without SQLite support, `--storage sqlite` fails with a clear error. `--storage auto` uses SQLite when compiled in and JSONL otherwise.

The default SQLite build uses the vendored SQLite amalgamation in `vendor/sqlite`, so `sqlite3.exe`, `sqlite3.h`, or `sqlite3.lib` do not need to be installed separately. Set `CAT_LIGHT_USE_BUNDLED_SQLITE=OFF` to use `find_package(SQLite3)` and a system SQLite development package instead.

## SQLite Target

The optional SQLite backend currently stores normalized events and latest sessions. The next step is to expand it into query-friendly tables:

- `events`
- `sessions`
- `token_usage`
- `context_snapshots`
- `quota_snapshots`
- `hook_installations`

The existing `sync_history`, `read_history_events`, and `read_history_sessions` functions are the backend boundary, so parser work should not need to change when the schema grows.
