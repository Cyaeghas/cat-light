param(
  [string]$Exe = "build\msvc-release\Release\cat-light.exe"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$exePath = Join-Path $root $Exe
if (-not (Test-Path -LiteralPath $exePath)) {
  throw "cat-light executable not found: $exePath"
}

$temp = Join-Path $env:TEMP ("cat-light-fixtures-" + [guid]::NewGuid().ToString("N"))
$env:CAT_LIGHT_DATA_DIR = Join-Path $temp "data"
$env:CODEX_HOME = Join-Path $root "tests\fixtures\codex"
$env:CLAUDE_CONFIG_DIR = Join-Path $root "tests\fixtures\claude"

$first = & $exePath sync-json --provider all --storage jsonl | ConvertFrom-Json
$second = & $exePath sync-json --provider all --storage jsonl | ConvertFrom-Json
$history = & $exePath history-json --provider all --storage jsonl --max-sessions 10 | ConvertFrom-Json
$summary = & $exePath history-summary-json --provider all --storage jsonl | ConvertFrom-Json
$summaryWindow = & $exePath history-summary-json --provider all --storage jsonl --since 2026-06-04 --until 2026-06-04 | ConvertFrom-Json
$summaryEmpty = & $exePath history-summary-json --provider all --storage jsonl --since 2026-06-05 | ConvertFrom-Json
$trends = & $exePath history-trends-json --provider all --storage jsonl --since 2026-06-04 --until 2026-06-04 | ConvertFrom-Json

if ($first.inserted -lt 8) {
  throw "expected fixture sync to insert at least 8 events, got $($first.inserted)"
}
if ($second.inserted -ne 0) {
  throw "expected second sync to dedupe all events, got inserted=$($second.inserted)"
}
if ($history.event_count -lt 8) {
  throw "expected history event_count >= 8, got $($history.event_count)"
}

$codex = $history.sessions | Where-Object { $_.provider -eq "codex" -and $_.session_id -eq "codex-fixture" } | Select-Object -First 1
if (-not $codex) {
  throw "missing codex fixture session"
}
if ($codex.state -ne "complete") {
  throw "expected codex fixture state complete, got $($codex.state)"
}
if ($codex.tokens.total -ne 1250) {
  throw "expected codex fixture total tokens 1250, got $($codex.tokens.total)"
}

$claude = $history.sessions | Where-Object { $_.provider -eq "claude" -and $_.session_id -eq "claude-fixture" } | Select-Object -First 1
if (-not $claude) {
  throw "missing claude fixture session"
}
if ($claude.state -ne "complete") {
  throw "expected claude fixture state complete, got $($claude.state)"
}
if ($claude.tokens.total -ne 3100) {
  throw "expected claude fixture total tokens 3100, got $($claude.tokens.total)"
}

if ($summary.tokens.total -ne 4350) {
  throw "expected summary total tokens 4350, got $($summary.tokens.total)"
}
$providerCount = @($summary.providers).Count
if ($providerCount -ne 2) {
  throw "expected 2 provider aggregates, got $providerCount"
}
$shellTool = $summary.tools | Where-Object { $_.key -eq "shell_command" } | Select-Object -First 1
if (-not $shellTool) {
  throw "missing shell_command tool aggregate"
}
if ($shellTool.events -ne 2) {
  throw "expected shell_command tool events 2, got $($shellTool.events)"
}
$bashTool = $summary.tools | Where-Object { $_.key -eq "Bash" } | Select-Object -First 1
if (-not $bashTool) {
  throw "missing Bash tool aggregate"
}
$cmakeCommand = $summary.commands | Where-Object { $_.key -eq "cmake" } | Select-Object -First 1
if (-not $cmakeCommand) {
  throw "missing cmake command aggregate"
}
if ($cmakeCommand.events -ne 1) {
  throw "expected cmake command events 1, got $($cmakeCommand.events)"
}
if ($summaryWindow.tokens.total -ne 4350) {
  throw "expected date-window summary total tokens 4350, got $($summaryWindow.tokens.total)"
}
if ($summaryEmpty.events -ne 0 -or $summaryEmpty.tokens.total -ne 0) {
  throw "expected empty summary after 2026-06-05, got events=$($summaryEmpty.events), tokens=$($summaryEmpty.tokens.total)"
}
$trendDays = @($trends.daily)
if ($trendDays.Count -ne 1) {
  throw "expected one trend day, got $($trendDays.Count)"
}
if ($trendDays[0].date -ne "2026-06-04") {
  throw "expected trend date 2026-06-04, got $($trendDays[0].date)"
}
if ($trendDays[0].tokens.total -ne 4350) {
  throw "expected trend total tokens 4350, got $($trendDays[0].tokens.total)"
}

"fixture tests passed"
