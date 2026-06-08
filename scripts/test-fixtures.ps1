param(
  [string]$Exe = "build\msvc-release\Release\cat-light.exe",
  [string]$Storage = "jsonl"
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

$live = & $exePath sessions-json --provider all --max-sessions 20 | ConvertFrom-Json
$first = & $exePath sync-json --provider all --storage $Storage | ConvertFrom-Json
$second = & $exePath sync-json --provider all --storage $Storage | ConvertFrom-Json
$history = & $exePath history-json --provider all --storage $Storage --max-sessions 10 | ConvertFrom-Json
$summary = & $exePath history-summary-json --provider all --storage $Storage | ConvertFrom-Json
$summaryWindow = & $exePath history-summary-json --provider all --storage $Storage --since 2026-06-04 --until 2026-06-04 | ConvertFrom-Json
$summaryDayTwo = & $exePath history-summary-json --provider all --storage $Storage --since 2026-06-05 --until 2026-06-05 | ConvertFrom-Json
$summaryEmpty = & $exePath history-summary-json --provider all --storage $Storage --since 2026-06-08 | ConvertFrom-Json
$trends = & $exePath history-trends-json --provider all --storage $Storage --since 2026-06-04 --until 2026-06-04 | ConvertFrom-Json
$doctor = & $exePath doctor-json --provider all | ConvertFrom-Json

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

if ($summary.tokens.total -ne 9775) {
  throw "expected summary total tokens 9775, got $($summary.tokens.total)"
}
$providerCount = @($summary.providers).Count
if ($providerCount -ne 2) {
  throw "expected 2 provider aggregates, got $providerCount"
}
$codexFailure = $history.sessions | Where-Object { $_.provider -eq "codex" -and $_.session_id -eq "codex-approval-failure" } | Select-Object -First 1
if (-not $codexFailure) {
  throw "missing codex approval failure fixture session"
}
if ($codexFailure.state -ne "error") {
  throw "expected codex approval failure state error, got $($codexFailure.state)"
}
$claudeError = $history.sessions | Where-Object { $_.provider -eq "claude" -and $_.session_id -eq "claude-error-fixture" } | Select-Object -First 1
if (-not $claudeError) {
  throw "missing claude error fixture session"
}
if ($claudeError.state -ne "error") {
  throw "expected claude error fixture state error, got $($claudeError.state)"
}
$codexDrift = $history.sessions | Where-Object { $_.provider -eq "codex" -and $_.session_id -eq "codex-schema-drift" } | Select-Object -First 1
if (-not $codexDrift) {
  throw "missing codex schema drift fixture session"
}
if ($codexDrift.state -ne "complete") {
  throw "expected codex schema drift state complete, got $($codexDrift.state)"
}
if ($codexDrift.tokens.total -ne 475 -or $codexDrift.context.limit -ne 1000) {
  throw "expected codex schema drift tokens/context 475/1000, got $($codexDrift.tokens.total)/$($codexDrift.context.limit)"
}
$sharedSessions = @($history.sessions | Where-Object { $_.provider -eq "codex" -and $_.session_id -eq "shared-session" })
if ($sharedSessions.Count -ne 2) {
  throw "expected same-name codex fixture files to remain 2 sessions, got $($sharedSessions.Count)"
}
$claudeDrift = $history.sessions | Where-Object { $_.provider -eq "claude" -and $_.session_id -eq "claude-schema-drift" } | Select-Object -First 1
if (-not $claudeDrift) {
  throw "missing claude schema drift fixture session"
}
if ($claudeDrift.state -ne "error") {
  throw "expected claude schema drift state error, got $($claudeDrift.state)"
}
if ($claudeDrift.tokens.total -ne 650) {
  throw "expected claude schema drift total tokens 650, got $($claudeDrift.tokens.total)"
}
$shellTool = $summary.tools | Where-Object { $_.key -eq "shell_command" } | Select-Object -First 1
if (-not $shellTool) {
  throw "missing shell_command tool aggregate"
}
if ($shellTool.events -ne 6) {
  throw "expected shell_command tool events 6, got $($shellTool.events)"
}
if ($shellTool.successes -ne 2 -or $shellTool.failures -ne 1) {
  throw "expected shell_command tool ok/fail 2/1, got $($shellTool.successes)/$($shellTool.failures)"
}
$bashTool = $summary.tools | Where-Object { $_.key -eq "Bash" } | Select-Object -First 1
if (-not $bashTool) {
  throw "missing Bash tool aggregate"
}
if ($bashTool.failures -ne 1) {
  throw "expected Bash failures 1, got $($bashTool.failures)"
}
$cmakeCommand = $summary.commands | Where-Object { $_.key -eq "cmake" } | Select-Object -First 1
if (-not $cmakeCommand) {
  throw "missing cmake command aggregate"
}
if ($cmakeCommand.events -ne 1) {
  throw "expected cmake command events 1, got $($cmakeCommand.events)"
}
if ($cmakeCommand.successes -ne 1 -or $cmakeCommand.duration_seconds -ne 2) {
  throw "expected cmake command ok/duration 1/2, got $($cmakeCommand.successes)/$($cmakeCommand.duration_seconds)"
}
$gitCommand = $summary.commands | Where-Object { $_.key -eq "git" } | Select-Object -First 1
if (-not $gitCommand) {
  throw "missing git command aggregate"
}
if ($gitCommand.successes -ne 1 -or $gitCommand.duration_seconds -ne 2) {
  throw "expected git command ok/duration 1/2, got $($gitCommand.successes)/$($gitCommand.duration_seconds)"
}
$npmCommand = $summary.commands | Where-Object { $_.key -eq "npm" } | Select-Object -First 1
if (-not $npmCommand) {
  throw "missing npm command aggregate"
}
if ($npmCommand.failures -ne 1 -or $npmCommand.duration_seconds -ne 1) {
  throw "expected npm command fail/duration 1/1, got $($npmCommand.failures)/$($npmCommand.duration_seconds)"
}
if ($summaryWindow.tokens.total -ne 4350) {
  throw "expected date-window summary total tokens 4350, got $($summaryWindow.tokens.total)"
}
if ($summaryDayTwo.tokens.total -ne 4000) {
  throw "expected second date-window summary total tokens 4000, got $($summaryDayTwo.tokens.total)"
}
if ($summaryEmpty.events -ne 0 -or $summaryEmpty.tokens.total -ne 0) {
  throw "expected empty summary after 2026-06-06, got events=$($summaryEmpty.events), tokens=$($summaryEmpty.tokens.total)"
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

$liveCodexDrift = $live.sessions | Where-Object { $_.provider -eq "codex" -and $_.session_id -eq "codex-schema-drift" } | Select-Object -First 1
if (-not $liveCodexDrift -or $liveCodexDrift.state -ne "complete") {
  throw "expected live codex schema drift session to be complete"
}
$liveShared = @($live.sessions | Where-Object { $_.provider -eq "codex" -and $_.session_id -eq "shared-session" })
if ($liveShared.Count -ne 2) {
  throw "expected live shared-session count 2, got $($liveShared.Count)"
}
$liveClaudeDrift = $live.sessions | Where-Object { $_.provider -eq "claude" -and $_.session_id -eq "claude-schema-drift" } | Select-Object -First 1
if (-not $liveClaudeDrift -or $liveClaudeDrift.state -ne "error") {
  throw "expected live claude schema drift session to be error"
}

$apiEvent = '{"provider":"codex","source":"api","session_id":"api-multi","instance_id":"codex:api-a","state":"thinking","tokens":{"prompt_tokens":10,"completion_tokens":5,"reasoning_tokens":2},"context":{"context_tokens":15,"context_window":100}}'
$apiEvent | & $exePath event --stdin | Out-Null
& $exePath event --provider codex --session-id api-multi --instance-id codex:api-b --state working --total-tokens 7 | Out-Null
$apiLive = & $exePath sessions-json --provider codex --max-sessions 20 | ConvertFrom-Json
$apiSessions = @($apiLive.sessions | Where-Object { $_.provider -eq "codex" -and $_.session_id -eq "api-multi" })
if ($apiSessions.Count -ne 2) {
  throw "expected wrapper/manual events with shared session_id to remain 2 instances, got $($apiSessions.Count)"
}
$apiA = $apiSessions | Where-Object { $_.instance_id -eq "codex:api-a" } | Select-Object -First 1
if (-not $apiA -or $apiA.tokens.total -ne 17 -or $apiA.context.percent -ne 15) {
  throw "expected API event token/context aliases to parse as total=17/context=15"
}

$codexLogs = $doctor | Where-Object { $_.provider -eq "codex-logs" } | Select-Object -First 1
if (-not $codexLogs -or -not $codexLogs.credential_ok -or $codexLogs.message -notmatch "files") {
  throw "expected doctor to report Codex local logs"
}
$claudeLogs = $doctor | Where-Object { $_.provider -eq "claude-logs" } | Select-Object -First 1
if (-not $claudeLogs -or -not $claudeLogs.credential_ok -or $claudeLogs.message -notmatch "files") {
  throw "expected doctor to report Claude local logs"
}
$hookDryRun = (& $exePath hook-install --provider all --shell powershell --dry-run) -join "`n"
if ($hookDryRun -notmatch "Codex notify" -or $hookDryRun -notmatch "Claude command") {
  throw "expected hook dry-run to include Codex and Claude install snippets"
}

"fixture tests passed [$Storage]"
