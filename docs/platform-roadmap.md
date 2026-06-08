# Product And Platform Roadmap

`cat-light` is intentionally shaped as a small C++ core plus optional presentation layers. The core CLI/server must stay useful on Windows, Linux, and macOS; native UI shells can be platform-specific and replaceable.

## Current Product Shape

The public prototype now has three working layers:

1. Core binary:
   - Collect quota.
   - Scan Codex and Claude logs.
   - Receive hooks and API wrapper events.
   - Sync history.
   - Serve local dashboard over `127.0.0.1`.

2. Windows desktop shell:
   - `cat-light-tray.exe` starts or supervises the core in the background.
   - `cat-light-float.exe` shows active Codex / Claude sessions without opening a browser.
   - The floating monitor is topmost, draggable, remembers position, has cat-light visual branding, and can register itself for current-user Windows startup.
   - Both shells open the local dashboard for detailed views.

3. Local dashboard:
   - Shows session cards first.
   - Shows history summaries and trends.
   - Stays browser-only for now, avoiding WebView dependencies.

## Milestones

### v0.1 Public Prototype

Done:

- CLI/status/history/sync/hook commands.
- Passive Codex and Claude JSONL scanners.
- Reversible Codex and Claude hook installation.
- JSONL history plus optional bundled SQLite build.
- Local dashboard.
- Windows tray and floating monitor.
- Public GitHub release artifacts.

### v0.2 Reliability And Parsing

Next priority:

- Add more sanitized real-world Codex / Claude JSONL fixtures.
- Harden parser edge cases for approvals, aborted turns, failed tools, partial logs, and provider schema drift.
- Improve token/context extraction, especially cache and reasoning fields.
- Improve Claude OAuth refresh and exact quota window handling.
- Add clearer `doctor` checks for hook health, startup registration, and stale local logs.

### v0.3 Storage And Analytics

Next after parser hardening:

- Expand SQLite into query-friendly tables for events, sessions, token usage, context snapshots, quota snapshots, tools, commands, and hook installs.
- Add migration/version metadata for local databases.
- Add provider/model/project/tool/command drilldown in the dashboard.
- Add token attribution where provider logs expose safe per-tool deltas.
- Add optional pricing/model metadata for estimated cost views.

### v0.4 Desktop UX

Desktop polish:

- Improve tray icon status rendering instead of using the default application icon.
- Add notification thresholds for waiting/error/quota pressure.
- Add installer or portable-app packaging notes.
- Decide whether WebView2 is worthwhile on Windows.
- Evaluate a separate cross-platform native shell.

CMake should remain the main build script. The project already avoids platform-heavy dependencies in the core:

- C++17 standard library for filesystem/time/string work.
- `#ifdef _WIN32` only where sockets, home paths, and chmod differ.
- `ws2_32` only on Windows.
- Optional SQLite through `CAT_LIGHT_ENABLE_SQLITE`; the default SQLite build uses the vendored amalgamation in `vendor/sqlite`.

Recommended build layers:

- Core CLI/server: always built by CMake on Windows, Linux, macOS.
- Optional storage backend: `CAT_LIGHT_ENABLE_SQLITE=ON`.
- Optional Windows tray target: `CAT_LIGHT_ENABLE_TRAY=ON`, producing `cat-light-tray.exe` on Windows.
- Optional Windows floating monitor target: `CAT_LIGHT_ENABLE_FLOAT=ON`, producing `cat-light-float.exe` on Windows.
- Candidate cross-platform UI shell: CopperSpice/Qt/wxWidgets, kept separate from the core CLI/server.
- Packaging: GitHub Actions release artifacts plus local scripts such as `scripts/package-windows.ps1`.

## Cross-Platform UI Decision

Do not move the core to a GUI framework yet.

- CopperSpice is attractive because it is CMake-based and C++-focused, but it is heavier and C++20-oriented.
- Qt and wxWidgets remain viable if we decide the native shell should be a real cross-platform app.
- WebView2/WKWebView/WebKitGTK can wrap the existing dashboard, but each adds platform-specific packaging cost.
- The current Win32 shells are intentionally small and can be replaced later without rewriting the core.

## Commands Useful For Presentation Layers

```powershell
cat-light serve --addr 127.0.0.1:8750
cat-light agent-waybar
cat-light state
cat-light sync
cat-light hook-status
```

Presentation layers should not understand Codex or Claude internals. They can poll `agent-waybar` or `/state`, then display status.
For history views, it can open the dashboard directly or read `/history-summary`.
