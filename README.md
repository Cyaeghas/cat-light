# cat-light

`cat-light` 是一个本地优先的 Codex / Claude Code 状态灯与用量指示器。目标不是只显示订阅 quota，而是同时观察多个 Codex / Claude Code 会话的运行状态、token 消耗、上下文长度和额度窗口。

当前代码已经进入第二阶段原型：除了 quota usage，也能从本地 Codex / Claude Code JSONL 日志、手动事件入口和可逆 hook 安装合并出多会话 agent 状态，并通过 `sync` 做本地去重历史同步。SQLite 后端已经支持 vendored amalgamation 构建，Windows 托盘壳也有第一版。

## 当前功能

- 读取 Codex CLI 的 `~/.codex/auth.json`。
- 读取 Claude Code 的 `~/.claude/.credentials.json`，并支持 `CLAUDE_CONFIG_DIR`。
- 请求 Codex 用量接口和 Claude OAuth 用量接口。
- 60 秒本地缓存，失败时尽量回退到旧缓存。
- Codex access token 失效时使用 refresh token 刷新并写回 `auth.json`。
- 输出 `status`、`json`、`waybar`。
- 输出 agent 状态：`state`、`sessions`、`sessions-json`、`agent-waybar`。
- 同步历史：`sync`、`sync-json`、`history`、`history-json`。
- 接收外部事件：`event --stdin`、CLI token/context 参数、`POST /event`。
- 可逆安装 hook：`hook-install`、`hook-uninstall`、`hook-status`、`hook-script`。
- 扫描 Codex 本地会话：`~/.codex/sessions/**/*.jsonl`。
- 扫描 Claude Code 本地会话：`~/.claude/projects/**/*.jsonl`。
- `doctor` 检查本机凭据、curl、缓存目录。
- `doctor` 检查 hook 最近事件、构建工具和 SQLite 环境。
- 可选 bundled SQLite 后端、Windows `cat-light-tray.exe` 托盘启动器和 `cat-light-float.exe` 桌面浮标。
- `serve` 启动 `127.0.0.1` 本地服务，首页显示 agent sessions 和 history summary，并提供 `/usage`、`/state`、`/sessions`、`/history`、`/history-summary`、`/sync`、`/event`。

这些用量接口属于未公开稳定接口，可能会变。程序只读取本地凭据并访问官方端点，不上传遥测。

## Agent 状态命令

```powershell
cat-light state
cat-light sessions
cat-light sessions-json
cat-light agent-waybar
cat-light sessions --provider codex --max-sessions 5
```

手动写入事件：

```powershell
cat-light event --provider codex --session-id demo --state thinking --detail "Thinking"
cat-light event --provider codex --session-id demo --state working --input-tokens 1000 --output-tokens 200 --context-used 1200 --context-limit 200000
```

从 stdin 写入 JSON 事件：

```powershell
'{"provider":"claude","session_id":"demo","state":"working"}' | cat-light event --stdin
```

HTTP 写入事件：

```powershell
cat-light serve
```

```text
POST http://127.0.0.1:8750/event
GET  http://127.0.0.1:8750/state
GET  http://127.0.0.1:8750/sessions
GET  http://127.0.0.1:8750/history-summary
GET  http://127.0.0.1:8750/history-trends?days=30
POST http://127.0.0.1:8750/sync
```

Windows PowerShell 下用 curl 发送 JSON，推荐写入临时文件再 `--data-binary @file`，避免命令行引号被吃掉。

## 历史同步

`sync` 会读取手动/hook 事件、本地 Codex rollout JSONL 和 Claude projects JSONL，写入去重后的历史事件流：

```powershell
cat-light sync --provider all
cat-light sync-json --provider all --storage jsonl
cat-light history --max-sessions 10
cat-light history-json --provider claude
cat-light history-summary
cat-light history-summary-json --provider codex
cat-light history-trends-json --days 30
```

`history-summary-json` 现在也包含 `tools` 和 `commands` 数组，用于观察工具调用和 shell 命令活动。

当前默认历史后端是 `%LOCALAPPDATA%\cat-light\history\events.jsonl` 和 `sessions.json`，已经有稳定去重键。也预留了可选 SQLite 后端，构建时打开：

```powershell
cmake -S . -B build\msvc-sqlite -DCAT_LIGHT_ENABLE_SQLITE=ON
```

或使用 preset：
```powershell
cmake --preset msvc-sqlite
cmake --build --preset msvc-sqlite
```

fixture 回归测试：

```powershell
.\scripts\test-fixtures.ps1
```

## Hook helper

安装、查看和卸载本机 hook：

```powershell
cat-light hook-install --provider all --shell powershell
cat-light hook-status --provider all
cat-light hook-uninstall --provider all
cat-light hook-install --provider claude --dry-run
cat-light hook-script --provider codex --shell powershell
```

`hook-install` 会写入 `%LOCALAPPDATA%\cat-light\hooks` 下的 helper 脚本，并修改 Claude `settings.json` / Codex `config.toml`。写入前会生成时间戳备份；`hook-uninstall` 只移除 cat-light 自己的 hook。Codex 原有 top-level `notify` 会保存到 `%LOCALAPPDATA%\cat-light\hooks\install.json` 并在卸载时恢复；PowerShell helper 会在运行 cat-light 后尽量继续调用原 notify。Claude helper 会从 hook stdin 读取 `UserPromptSubmit`、`PreToolUse`、`PostToolUse`、`Notification`、`Stop`、`SessionEnd` 等事件并归一化成 `cat-light.event.v1`。

## 下一阶段重点

- 扩展 SQLite 查询表，继续细化项目、模型、工具和命令维度。
- 增加更多真实脱敏 fixtures，覆盖 Codex rollout 和 Claude JSONL 的边缘形态。
- 将 Windows tray 继续演进为跨平台 tray / floating UI。

详见：

- `docs/comparison.md`
- `docs/hook-contract.md`
- `docs/storage.md`
- `docs/platform-roadmap.md`

## Windows 构建

### MSVC / Visual Studio

你这台机器上 Visual Studio 2022 Community 和 VS 自带 CMake 已经存在，可以直接用：

```powershell
.\scripts\build-msvc.ps1
```

手动方式：

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build\msvc-release -G "Visual Studio 17 2022" -A x64
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build\msvc-release --config Release
```

CMake Presets 方式：

```powershell
cmake --preset msvc-release
cmake --build --preset msvc-release
```

可选 SQLite 后端：

```powershell
cmake -S . -B build\msvc-sqlite -G "Visual Studio 17 2022" -A x64 -DCAT_LIGHT_ENABLE_SQLITE=ON
cmake --build build\msvc-sqlite --config Release
```

产物：

```text
build\msvc-release\Release\cat-light.exe
build\msvc-release\Release\cat-light-tray.exe
build\msvc-release\Release\cat-light-float.exe
```

### MSYS2 / UCRT64

如果 UCRT64 工具链还没装：

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
```

然后：

```powershell
.\scripts\build-msys2.ps1
```

## 使用

```powershell
cat-light doctor
cat-light status
cat-light json
cat-light waybar
cat-light serve --addr 127.0.0.1:8750
```

常用参数：

```powershell
cat-light status --provider codex
cat-light status --provider claude
cat-light status --offline
cat-light status --refresh
cat-light status --no-token-refresh
cat-light sessions --max-sessions 10
```

打开本地仪表盘：

```text
http://127.0.0.1:8750
```

Windows 桌面浮标：

```powershell
build\msvc-release\Release\cat-light-float.exe
```

浮标会常驻在屏幕右下角、保持置顶、可拖动，直接显示当前 Codex / Claude Code 会话状态。双击打开本地 dashboard，右键可同步历史、查看 hook 状态、刷新、重置位置或退出；拖动后的位置会保存到 `%LOCALAPPDATA%\cat-light\float.ini`。

## Release

GitHub Actions 会在 `main` 和 PR 上构建 SQLite-enabled 二进制。推送 `v*` tag 时会自动创建 GitHub Release：

```powershell
git tag v0.1.0
git push origin v0.1.0
```

Windows artifact 包含 `cat-light.exe`、`cat-light-tray.exe` 和 `cat-light-float.exe`。

## Waybar

```jsonc
"custom/cat-light": {
  "exec": "cat-light waybar",
  "return-type": "json",
  "interval": 300,
  "tooltip": true
}
```

## 参考方向

- `mryll/codexbar`: 读取 Codex OAuth 凭据并输出状态栏 JSON。
- `mryll/claudebar`: 读取 Claude Code OAuth 凭据并输出状态栏 JSON。
- `mm7894215/TokenTracker`: 本地聚合、多工具仪表盘方向。
- `Bayern4ever-dot/code-light`: 轻量指示器方向。

## 之后的 UI

- 继续打磨 Windows 托盘和桌面浮标外壳；跨平台 UI 可选 CopperSpice/Qt/wxWidgets。
- 托盘优先显示 agent 状态，quota 作为次要信息。
- 本地仪表盘显示每个会话、项目、模型、token 和上下文占用。
