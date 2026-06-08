# Windows Installation

This guide covers the current Windows prototype. The release archive contains a CLI/server binary plus two optional UI helpers.

## Download

Download the latest Windows archive from:

```text
https://github.com/Cyaeghas/cat-light/releases
```

Unzip it into a directory you control, for example:

```text
C:\Tools\cat-light
```

Expected files:

```text
cat-light.exe
cat-light-tray.exe
cat-light-float.exe
README.md
README.zh-CN.md
docs\install-windows.md
```

## First Run

Open PowerShell in the extracted directory:

```powershell
.\cat-light.exe doctor
.\cat-light.exe state
.\cat-light-float.exe
```

The floating monitor starts a local dashboard server on:

```text
http://127.0.0.1:8750
```

## UI Helpers

### Floating Monitor

```powershell
.\cat-light-float.exe
```

- Draggable, topmost desktop monitor.
- Double-click to open the dashboard.
- Right-click to sync, inspect hook status, refresh, reset position, enable/disable startup, or quit.
- Position is saved in `%LOCALAPPDATA%\cat-light\float.ini`.
- Startup uses the current user Run key and does not require administrator rights.

### Tray Launcher

```powershell
.\cat-light-tray.exe
```

- Left-click the tray icon to open the dashboard.
- Right-click for menu actions.
- Starts `cat-light.exe serve --addr 127.0.0.1:8750` in the background.

## Optional Hooks

Install reversible Codex and Claude Code hooks:

```powershell
.\cat-light.exe hook-install --provider all --shell powershell
.\cat-light.exe hook-status --provider all
```

Uninstall:

```powershell
.\cat-light.exe hook-uninstall --provider all
```

Hook helpers are stored under:

```text
%LOCALAPPDATA%\cat-light\hooks
```

User config files are backed up before modification.

## Data Locations

```text
%LOCALAPPDATA%\cat-light\cache
%LOCALAPPDATA%\cat-light\history
%LOCALAPPDATA%\cat-light\hooks
%LOCALAPPDATA%\cat-light\float.ini
```

The tool reads local Codex / Claude Code credentials and local session logs. It does not upload prompts, responses, tool outputs, file contents, or telemetry.

## Build From Source

```powershell
cmake --preset msvc-release
cmake --build --preset msvc-release
```

SQLite-enabled build:

```powershell
cmake --preset msvc-sqlite
cmake --build --preset msvc-sqlite
```

## Package Locally

After building:

```powershell
.\scripts\package-windows.ps1 -BuildDir build\msvc-sqlite -Version dev
```

The zip archive is written under `dist\packages`.
