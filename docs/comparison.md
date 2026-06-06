# Comparison And Missing Pieces

This file records what the reference projects do and what `cat-light` still lacks.

## Current Cat-Light Prototype

Implemented:

- C++17 single-binary CLI.
- Codex quota usage via `~/.codex/auth.json` and ChatGPT backend usage endpoint.
- Claude quota endpoint scaffold.
- 60 second cache.
- `status`, `json`, `waybar`, `doctor`, `serve`.
- Local HTTP dashboard skeleton.
- Agent event model and append-only `events.jsonl` storage.
- `event --stdin` and CLI event flags, including token/context fields.
- `state`, `sessions`, `sessions-json`, `agent-waybar`.
- HTTP `POST /event`, `GET /state`, `GET /sessions`.
- Codex local JSONL scan under `~/.codex/sessions`.
- Claude local JSONL scan under `~/.claude/projects`.
- Multi-session merge by `provider:session_id`.
- Basic token/context extraction from local logs.
- `sync`, `sync-json`, `history`, and `history-json` with deduped history JSONL.
- `history-summary` and `history-summary-json` aggregating tokens/events by provider, model, and project from merged sessions.
- Tool and shell-command activity aggregates in history summaries.
- `--since`, `--until`, and `--days` filters for history readers.
- Daily token/event trend buckets and `history-trends-json`.
- Local dashboard History view with range filtering and HTTP `/history`, `/history-summary`, `/history-trends`, `/sync` endpoints.
- History storage backend boundary with default JSONL and optional CMake SQLite backend.
- Fixture tests for representative Codex rollout and Claude projects JSONL.
- Reversible hook install/uninstall/status for Claude `settings.json` and Codex `config.toml`.
- Hook helper generation with `hook-install` and `hook-script`; Codex PowerShell helper preserves and chains the original notify command when possible.
- Dashboard homepage focused on agent sessions.

Not implemented yet:

- Token attribution by tool.
- SQLite persistence is optional and early; richer query/aggregation tables are still missing.
- Native tray/floating window.
- More real-world Codex / Claude JSONL fixtures and parser edge cases.
- Full context window limit detection.
- Richer trend charts with provider/model drilldown.

## code-light

Useful ideas:

- Product shape is closest to what we want: tray icon, floating desktop widget, local dashboard.
- Tracks both current status and token usage.
- Uses a status model with `idle`, `working`, `done`, `waiting`, `error`, `quota_warning`, `offline`.
- Tracks per-session fields: provider, model, project path, session id, last activity, tokens, message/detail.
- Persists state in SQLite tables for current agent status, sessions, token usage, task history, and quota snapshots.
- Claude Code monitor reads `~/.claude/projects/**/*.jsonl`.
- Codex monitor reads `~/.codex/sessions`.
- Codex tail events expose lifecycle signals such as `task_started`, `task_complete`, `turn_aborted`, `reasoning`, `function_call`, `function_call_output`, `custom_tool_call`.
- Claude tail events infer state from assistant stop reasons, tool use/results, hook attachments, and `stop_hook_summary`.
- Windows process detector enumerates VS Code windows and can focus related windows.

What we should copy conceptually:

- Separate `agent session state` from `quota window state`.
- Keep multiple recent sessions per provider.
- Derive detail text from latest meaningful event: thinking, running command, editing file, waiting for approval, complete, aborted.
- Store history locally.

## mryll/codexbar

Useful ideas:

- Focused Waybar widget for Codex subscription usage.
- Reads `~/.codex/auth.json`.
- Refreshes Codex OAuth tokens.
- Calls ChatGPT backend usage endpoint.
- Tracks session, weekly, code-review usage, and credits.
- 60 second response cache.
- Rich format placeholders and Pango tooltip bars.
- Pacing indicators based on usage percent vs elapsed window percent.

What we already partially have:

- Codex auth reading.
- Codex OAuth refresh.
- Quota fetching.
- Basic cache.
- Basic Waybar JSON.
- Basic pacing.

Missing compared with codexbar:

- Format placeholders.
- Rich tooltip bars.
- `remaining` mode.
- Per-window color / marker logic.
- More complete review quota and credits formatting.
- Broader fixture tests and hardening.

## mryll/claudebar

Useful ideas:

- Focused Waybar widget for Claude Code usage limits.
- Reads Claude CLI credentials.
- Auto-refreshes OAuth tokens.
- Calls `api.anthropic.com/api/oauth/usage`.
- Tracks 5h, 7d, Sonnet-only, and extra usage.
- Uses cache and stale fallback to survive API rate limits.
- Has rich formatting, theme integration, and pacing.

What we already partially have:

- Claude credential path detection.
- Claude usage endpoint request scaffold.
- Basic cache / fallback.

Missing compared with claudebar:

- Claude OAuth refresh.
- Exact response-shape handling for `five_hour`, `seven_day`, `seven_day_sonnet`, and `extra_usage`.
- Rich tooltip bars and placeholders.
- Rate-limit-friendly stale indicator.
- Tests/fixtures.

## TokenTracker

Useful ideas:

- Tracks token usage across many AI coding tools.
- Local-first: token counts and timestamps only.
- Auto-installs hooks where tools support hooks.
- Uses passive readers when hooks are not appropriate.
- Aggregates into local SQLite and dashboard snapshots.
- Has native macOS menu bar and Windows tray apps.
- Supports Claude Code via SessionEnd hook in `settings.json`.
- Supports Codex CLI via TOML `notify` hook in `config.toml`.
- Parses Codex rollout JSONL for token counts, context breakdown, tool attribution, command stats, and dedupe.
- Uses a pricing engine with model context/pricing metadata.
- Has `status`, `doctor`, `sync`, and uninstall flows for hook health.

What we should copy conceptually:

- `init`/`hook install` should be explicit and reversible.
- Always back up user config before modifying hooks.
- Hook should trigger a local sync, but parsing should happen from local logs whenever possible.
- Do not store prompts, responses, tool outputs, or file contents.
- Use dedupe keys that survive providers without stable request ids.
- Keep hook status visible in `doctor`.

## Reframed Product Definition

`cat-light` should have three layers:

1. Runtime state layer:
   - Live session states: `starting`, `thinking`, `working`, `waiting`, `complete`, `error`, `idle`, `stale`.
   - Multiple instances per provider.
   - Sources: hooks, passive JSONL readers, process/window detection.

2. Usage layer:
   - Local tokens: input, output, cache read/write, reasoning, total.
   - Context: used, limit, remaining, percent.
   - Quota windows: 5h/session, 7d/weekly, review, per-model, credits.

3. Presentation layer:
   - CLI and JSON.
   - Waybar/statebar format.
   - Local dashboard.
   - Native tray/floating UI later.

The current code now has a first pass of layer 1 plus the earlier layer 2 quota prototype. The next milestone should make hooks reversible and add durable history/tests.
