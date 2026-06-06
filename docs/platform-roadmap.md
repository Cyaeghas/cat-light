# Platform And Tray Roadmap

`cat-light` is intentionally shaped as a C++ core plus small presentation layers. The current CLI/server binary is already a useful backend for a future tray app.

## Can This Become A Tray App?

Yes. The intended shape is:

1. Core binary:
   - Collect quota.
   - Scan Codex and Claude logs.
   - Receive hooks and API wrapper events.
   - Sync history.
   - Serve local dashboard over `127.0.0.1`.

2. Tray shell:
   - Starts or supervises the core in the background.
   - Shows the highest-priority state as an icon/color.
   - Opens the local dashboard or a small WebView.
   - Exposes menu actions: sync, hook status, install/uninstall hooks, quit.

3. UI surface:
   - Minimal local web dashboard at first.
   - Later: WebView2 on Windows, WKWebView on macOS, WebKitGTK/Qt WebEngine on Linux, or one cross-platform toolkit.

The first tray target is now a Windows Win32 launcher. It starts the localhost dashboard server, exposes tray actions for opening the dashboard, syncing history, checking hook status, and quitting. A cross-platform tray can come later through Qt or wxWidgets.

## Cross-Platform Build

CMake should remain the main build script. The project already avoids platform-heavy dependencies in the core:

- C++17 standard library for filesystem/time/string work.
- `#ifdef _WIN32` only where sockets, home paths, and chmod differ.
- `ws2_32` only on Windows.
- Optional SQLite through `CAT_LIGHT_ENABLE_SQLITE`; the default SQLite build uses the vendored amalgamation in `vendor/sqlite`.

Recommended build layers:

- Core CLI/server: always built by CMake on Windows, Linux, macOS.
- Optional storage backend: `CAT_LIGHT_ENABLE_SQLITE=ON`.
- Optional Windows tray target: `CAT_LIGHT_ENABLE_TRAY=ON`, producing `cat-light-tray.exe` on Windows.
- Packaging: separate CMake install/CPack or per-platform scripts.

## Current Commands Useful For A Tray Shell

```powershell
cat-light serve --addr 127.0.0.1:8750
cat-light agent-waybar
cat-light state
cat-light sync
cat-light hook-status
```

The tray does not need to understand Codex or Claude internals. It can poll `agent-waybar` or `/state`, then display status.
For history views, it can open the dashboard directly or read `/history-summary`.
