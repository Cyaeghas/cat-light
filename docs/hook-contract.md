# Hook And Event Contract

`cat-light` should accept events from official hooks, unofficial API wrappers, and passive log parsers through the same internal schema.

## State Model

Canonical states:

- `starting`: process/session just started or prompt submitted.
- `thinking`: model is reasoning or streaming thought-like content.
- `working`: agent is generating text, running a tool, editing files, or processing tool output.
- `waiting`: waiting for approval, credentials, user input, or external process.
- `complete`: task/turn ended successfully.
- `error`: tool failure, abort, hook error, auth failure, or parse failure.
- `idle`: no active work in this session.
- `stale`: last signal is too old to trust.

Status priority for aggregate tray display:

1. `error`
2. `waiting`
3. `working`
4. `thinking`
5. `starting`
6. `complete`
7. `idle`
8. `stale`

## Event Schema

Every source should be normalized into this shape:

```json
{
  "schema": "cat-light.event.v1",
  "provider": "codex",
  "source": "hook",
  "session_id": "abc123",
  "instance_id": "codex:abc123",
  "project_path": "C:/repo/project",
  "pid": 12345,
  "model": "gpt-5-codex",
  "state": "working",
  "phase": "tool_call",
  "detail": "Running shell command",
  "tool_name": "shell_command",
  "timestamp": "2026-06-04T12:00:00Z",
  "tokens": {
    "input": 0,
    "output": 0,
    "cache_read": 0,
    "cache_write": 0,
    "reasoning": 0,
    "total": 0
  },
  "context": {
    "used": 0,
    "limit": 0,
    "percent": 0
  },
  "raw_kind": "task_started"
}
```

Privacy rule: event storage must not persist prompt text, assistant text, command output, tool output, file contents, or environment variables. Store numbers, ids, paths, timestamps, model names, state labels, and sanitized tool names only.

## CLI And HTTP Ingestion

Implemented commands:

```powershell
cat-light event --provider codex --state starting --session-id <id>
cat-light event --provider codex --state working --input-tokens 100 --output-tokens 20 --context-used 120 --context-limit 200000
cat-light event --stdin
cat-light state
cat-light sessions
cat-light sessions-json
cat-light agent-waybar
cat-light hook-install --provider codex
cat-light hook-install --provider claude
cat-light hook-status
cat-light hook-uninstall --provider codex
cat-light hook-uninstall --provider claude
cat-light hook-script --provider codex
cat-light hook-script --provider claude
```

Implemented or planned local HTTP endpoints:

```text
POST /event
GET  /state
GET  /sessions
GET  /history
GET  /history-summary
GET  /usage
POST /sync
GET  /health
```

`POST /event` is useful for unofficial API wrappers. Hook scripts can use either CLI or HTTP depending on what is easier in that runtime.

## Codex Sources

Primary passive source:

```text
~/.codex/sessions/**/*.jsonl
~/.codex/sessions/**/rollout-*.jsonl
```

Important Codex event shapes to normalize:

- `event_msg.payload.type = task_started` -> `starting` / `working`.
- `event_msg.payload.type = task_complete` -> `complete`.
- `event_msg.payload.type = turn_aborted` -> `error`.
- `event_msg.payload.type = token_count` -> token/context metrics.
- `event_msg.payload.type = agent_message`, `phase = commentary` -> `working`.
- `event_msg.payload.type = agent_message`, `phase = final_answer` -> `complete`.
- `response_item.payload.type = reasoning` -> `thinking`.
- `response_item.payload.type = function_call` -> `working` or `waiting` if approval is required.
- `response_item.payload.type = function_call_output` -> `working` or `error`.
- `response_item.payload.type = custom_tool_call` -> `working`.
- `response_item.payload.type = custom_tool_call_output` -> `working` or `error`.

Hook:

```toml
notify = ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "C:\\Users\\you\\AppData\\Local\\cat-light\\hooks\\cat-light-codex-hook.ps1"]
```

Current install:

- `cat-light hook-install --provider codex` writes a helper script and edits `~/.codex/config.toml` or `CODEX_HOME/config.toml`.
- The helper accepts notify text/JSON from stdin or argv, maps obvious approval/complete/error/thinking text, and writes a normalized event.
- If a top-level `notify` already exists, it is saved in `%LOCALAPPDATA%/cat-light/hooks/install.json` or the platform data dir.
- PowerShell helper scripts try to call the original notify after recording the cat-light event.
- A timestamped backup is written before changing an existing config.
- `cat-light hook-uninstall --provider codex` removes cat-light notify and restores the saved original notify if present.
- The minimal TOML editor handles top-level `notify = [...]`; nested/table-scoped notify is intentionally not rewritten.

## Claude Code Sources

Primary passive source:

```text
~/.claude/projects/**/*.jsonl
```

Important Claude shapes to normalize:

- Assistant message with no stop reason -> `working`.
- Assistant content block `type = thinking` -> `thinking`.
- Assistant stop reason `tool_use` with unresolved tool result -> `waiting`.
- Assistant stop reason `tool_use` with resolved tool result -> `working`.
- Assistant stop reason `end_turn` -> `complete`.
- User event with `tool_result` -> `working` or `error`.
- User event with `promptId` -> `starting` / `working`.
- System subtype `stop_hook_summary` -> `complete`.
- Attachment `hookEvent = UserPromptSubmit` -> `starting`.
- Attachment `hookEvent = PreToolUse` -> `working`.
- Attachment `hookEvent = Stop` -> `complete`.
- Attachment `type = hook_non_blocking_error` -> `error`.

Hook:

```json
{
  "hooks": {
    "SessionEnd": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"C:\\Users\\you\\AppData\\Local\\cat-light\\hooks\\cat-light-claude-hook.ps1\""
          }
        ]
      }
    ]
  }
}
```

For live state, we may also support `UserPromptSubmit`, `PreToolUse`, `PostToolUse`, `Stop`, and `Notification` when available. `SessionEnd` alone is enough for sync and token rollups, but not enough for real-time thinking/working.

Current install:

- `cat-light hook-install --provider claude` writes a helper script and edits `~/.claude/settings.json` or `CLAUDE_CONFIG_DIR/settings.json`.
- The helper reads hook JSON from stdin, maps `UserPromptSubmit`, `PreToolUse`, `PostToolUse`, `Notification`, `Stop`, `SubagentStop`, and `SessionEnd`, and writes a normalized event.
- Existing user hooks are preserved; reinstall first removes old cat-light commands to avoid duplicates.
- A timestamped backup is written before changing an existing settings file.
- `cat-light hook-uninstall --provider claude` removes only cat-light command hooks and leaves other hooks in place.
- UTF-8 BOM settings files are accepted.

## Multiple Instances

Session identity should be:

```text
provider + session_id
```

When session id is missing:

```text
provider + project_path + pid + start_time
```

The dashboard and JSON output should keep all live sessions. The tray/Waybar summary should select the highest-priority state and include counts:

```text
Codex: 2 working, 1 waiting | Claude: complete
```

## Storage

Current implementation uses two append-only JSONL locations:

```text
%LOCALAPPDATA%/cat-light/events.jsonl
~/.local/share/cat-light/events.jsonl
~/Library/Application Support/cat-light/events.jsonl
```

`events.jsonl` stores direct manual/API/hook events. `cat-light sync` reads those events plus passive Codex/Claude logs and writes deduped history:

```text
%LOCALAPPDATA%/cat-light/history/events.jsonl
%LOCALAPPDATA%/cat-light/history/sessions.json
```

The history JSONL envelope stores a stable dedupe key plus the normalized `cat-light.event.v1` event. This is the temporary backend for parser and sync development.

Longer term, use SQLite:

- `events`
- `sessions`
- `token_usage`
- `context_snapshots`
- `quota_snapshots`
- `hook_installations`

SQLite is acceptable for a C++ single-binary app by vendoring the SQLite amalgamation.
