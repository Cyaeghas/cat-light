# cat-light

[![build](https://github.com/Cyaeghas/cat-light/actions/workflows/build.yml/badge.svg)](https://github.com/Cyaeghas/cat-light/actions/workflows/build.yml)

`cat-light` is a local-first status monitor for Codex and Claude Code. It watches active coding-agent sessions, token usage, context pressure, hooks, and history, then exposes the result through a CLI, local dashboard, Waybar JSON, Windows tray, and a compact floating desktop monitor.

The project is written in C++ with CMake. The core stays dependency-light and cross-platform; richer native UI shells are kept as optional targets.

## Why

Most existing status-bar tools focus on one narrow signal: quota usage, a single provider, or a single desktop shell. `cat-light` aims to combine four signals in one local tool:

| Layer | What it tracks |
| --- | --- |
| Live state | Multiple Codex / Claude Code sessions, including `starting`, `thinking`, `working`, `waiting`, `complete`, `error`, `idle`, and `stale`. |
| Token usage | Input/output/cache/reasoning totals where available, plus session-level token totals from local logs. |
| Context | Context used, context limit, remaining window, and percent used when the provider log exposes it. |
| History | Deduped local events, daily trends, provider/model/project summaries, tool calls, and shell command activity. |

The tool reads local credentials and local session logs. It does not upload prompts, responses, tool outputs, file contents, or telemetry.

## Current Shape

| Surface | Status | Notes |
| --- | --- | --- |
| CLI | Working | `status`, `json`, `waybar`, `state`, `sessions`, `history`, `sync`, `doctor`. |
| Hooks | Working prototype | Reversible Codex `notify` and Claude `settings.json` hook install/uninstall. |
| Local dashboard | Working prototype | Served on `127.0.0.1`; shows sessions, history summaries, and trends. |
| Windows tray | Working prototype | Starts the local server and exposes quick actions. |
| Windows floating monitor | Working prototype | Always-on-top desktop widget for active Codex / Claude state. |
| SQLite backend | Optional prototype | Bundled SQLite amalgamation build path is available. |
| Cross-platform native UI | Planned | CopperSpice, Qt, and wxWidgets are candidates for a future shell. |

## Preview

The Windows floating monitor is designed for the “small desktop status tile” workflow:

```text
cat-light                                  21:18
waiting

Claude 2 complete, 1 starting
Codex  2 complete, 1 waiting

Codex 2026-06- [waiting]
project: 2026/06/04
```

It stays topmost, can be dragged, remembers its position in `%LOCALAPPDATA%\cat-light\float.ini`, and opens the dashboard on double-click. Right-click actions include sync, hook status, refresh, reset position, and quit.

## Quick Start

Build with Visual Studio / MSVC:

```powershell
cmake --preset msvc-release
cmake --build --preset msvc-release
```

Build with bundled SQLite enabled:

```powershell
cmake --preset msvc-sqlite
cmake --build --preset msvc-sqlite
```

Run the most useful commands:

```powershell
build\msvc-release\Release\cat-light.exe doctor
build\msvc-release\Release\cat-light.exe state
build\msvc-release\Release\cat-light.exe sessions --max-sessions 8
build\msvc-release\Release\cat-light.exe serve --addr 127.0.0.1:8750
```

Start the Windows UI helpers:

```powershell
build\msvc-release\Release\cat-light-tray.exe
build\msvc-release\Release\cat-light-float.exe
```

Open the dashboard:

```text
http://127.0.0.1:8750
```

## Agent State

`cat-light` merges live events from hooks, passive JSONL scans, and manual events into per-session state.

```powershell
cat-light state
cat-light sessions
cat-light sessions-json
cat-light agent-waybar
cat-light sessions --provider codex --max-sessions 5
```

Manual event input is useful when wrapping unofficial APIs or custom launchers:

```powershell
cat-light event --provider codex --session-id demo --state thinking --detail "Thinking"
cat-light event --provider codex --session-id demo --state working --input-tokens 1000 --output-tokens 200 --context-used 1200 --context-limit 200000
```

JSON event input:

```powershell
'{"provider":"claude","session_id":"demo","state":"working"}' | cat-light event --stdin
```

HTTP event input:

```text
POST http://127.0.0.1:8750/event
GET  http://127.0.0.1:8750/state
GET  http://127.0.0.1:8750/sessions
```

## History And Storage

`sync` reads manual events, hook events, Codex session JSONL, and Claude project JSONL, then writes a deduped local history stream.

```powershell
cat-light sync --provider all
cat-light sync-json --provider all --storage jsonl
cat-light history --max-sessions 10
cat-light history-json --provider claude
cat-light history-summary
cat-light history-summary-json --provider codex
cat-light history-trends-json --days 30
```

Default JSONL storage lives under `%LOCALAPPDATA%\cat-light\history`. SQLite can be enabled at build time:

```powershell
cmake -S . -B build\msvc-sqlite -G "Visual Studio 17 2022" -A x64 -DCAT_LIGHT_ENABLE_SQLITE=ON
cmake --build build\msvc-sqlite --config Release
```

## Hooks

Install, inspect, and remove local hooks:

```powershell
cat-light hook-install --provider all --shell powershell
cat-light hook-status --provider all
cat-light hook-uninstall --provider all
cat-light hook-install --provider claude --dry-run
cat-light hook-script --provider codex --shell powershell
```

Hook installation is designed to be reversible:

- Helper scripts are written under `%LOCALAPPDATA%\cat-light\hooks`.
- Claude `settings.json` and Codex `config.toml` are backed up before modification.
- Codex top-level `notify` is preserved in the install manifest and restored on uninstall.
- The PowerShell helper tries to continue calling the original Codex notify command after recording a `cat-light` event.

## Waybar

```jsonc
"custom/cat-light": {
  "exec": "cat-light agent-waybar",
  "return-type": "json",
  "interval": 5,
  "tooltip": true
}
```

Quota-oriented output is also available:

```jsonc
"custom/cat-light-quota": {
  "exec": "cat-light waybar",
  "return-type": "json",
  "interval": 300,
  "tooltip": true
}
```

## Build Outputs

Windows MSVC release builds produce:

```text
build\msvc-release\Release\cat-light.exe
build\msvc-release\Release\cat-light-tray.exe
build\msvc-release\Release\cat-light-float.exe
```

GitHub Actions builds SQLite-enabled binaries on Windows, Linux, and macOS. The Windows artifact contains all three Windows executables.

## Reference Projects

`cat-light` borrows ideas from several useful projects while trying to combine them into one local-first monitor.

| Project | What is useful for `cat-light` |
| --- | --- |
| [Bayern4ever-dot/code-light](https://github.com/Bayern4ever-dot/code-light) | Product direction: lightweight indicator, tray/floating widget, local dashboard, active status plus token usage. |
| [mryll/codexbar](https://github.com/mryll/codexbar) | Codex auth reading, OAuth refresh, quota endpoint usage, cache fallback, Waybar formatting. |
| [mryll/claudebar](https://github.com/mryll/claudebar) | Claude Code quota tracking, cache/stale handling, status-bar presentation. |
| [mm7894215/TokenTracker](https://github.com/mm7894215/TokenTracker) | Local aggregation, hook install flows, SQLite-backed history, multi-tool token tracking. |
| [copperspice/copperspice](https://github.com/copperspice/copperspice) | Candidate future cross-platform C++ GUI shell; attractive but heavier and C++20-oriented. |

More detailed notes live in [docs/comparison.md](docs/comparison.md).

## Roadmap

Near-term:

- Add richer SQLite query tables for provider, model, project, tool, and command drilldowns.
- Improve token attribution by tool and command.
- Add more real-world sanitized Codex / Claude JSONL fixtures.
- Polish the Windows floating monitor and tray shell.
- Add a packaged release flow with clearer installation steps.

Longer-term:

- Cross-platform tray or floating UI shell.
- More complete Claude OAuth refresh and quota response handling.
- Pricing/model metadata for estimated cost views.
- Better dashboard charts for trends, context, and session history.

## Development

Run fixture tests:

```powershell
.\scripts\test-fixtures.ps1
.\scripts\test-fixtures.ps1 -Exe build\msvc-sqlite\Release\cat-light.exe -Storage sqlite
```

Useful docs:

- [Hook contract](docs/hook-contract.md)
- [Storage design](docs/storage.md)
- [Platform roadmap](docs/platform-roadmap.md)
- [Reference comparison](docs/comparison.md)
