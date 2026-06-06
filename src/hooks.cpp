#include "hooks.hpp"

#include "agent.hpp"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace catlight {
namespace {

struct NotifyAssignment {
  bool found = false;
  size_t begin = 0;
  size_t end = 0;
  std::string text;
  std::vector<std::string> argv;
};

std::string default_shell(const Options &options) {
  if (!options.hook_shell.empty()) {
    return options.hook_shell;
  }
#ifdef _WIN32
  return "powershell";
#else
  return "sh";
#endif
}

bool wants_provider(const Options &options, const std::string &provider) {
  return provider_selected(options, provider);
}

std::filesystem::path hook_dir() {
  return data_root() / "hooks";
}

std::filesystem::path metadata_path() {
  return hook_dir() / "install.json";
}

std::filesystem::path script_path(const std::string &provider, const std::string &shell) {
  const std::string ext = shell == "sh" ? ".sh" : ".ps1";
  return hook_dir() / ("cat-light-" + provider + "-hook" + ext);
}

std::filesystem::path claude_settings_path() {
  if (auto config = env_var("CLAUDE_CONFIG_DIR")) {
    return std::filesystem::path(*config) / "settings.json";
  }
  return home_dir() / ".claude" / "settings.json";
}

std::filesystem::path codex_config_path() {
  if (auto codex_home = env_var("CODEX_HOME")) {
    return std::filesystem::path(*codex_home) / "config.toml";
  }
  return home_dir() / ".codex" / "config.toml";
}

std::string safe_timestamp() {
  std::time_t t = Clock::to_time_t(Clock::now());
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y%m%d-%H%M%S");
  return out.str();
}

std::string ps_quote(const std::string &value) {
  std::string out = "'";
  for (char c : value) {
    if (c == '\'') {
      out += "''";
    } else {
      out.push_back(c);
    }
  }
  out += "'";
  return out;
}

std::string sh_quote(const std::string &value) {
  std::string out = "'";
  for (char c : value) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out.push_back(c);
    }
  }
  out += "'";
  return out;
}

std::string json_quote(const std::string &value) {
  return Json(value).dump();
}

std::string toml_quote(const std::string &value) {
  std::string out = "\"";
  for (char c : value) {
    if (c == '\\' || c == '"') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  out += "\"";
  return out;
}

std::string absolute_exe(const std::filesystem::path &exe_path) {
  std::error_code ec;
  auto abs = std::filesystem::absolute(exe_path, ec);
  return (ec ? exe_path : abs).string();
}

std::string json_string(const Json *value, std::string fallback = "") {
  if (!value || !value->is_string()) {
    return fallback;
  }
  return value->as_string();
}

bool json_bool(const Json *value, bool fallback = false) {
  if (!value || !value->is_bool()) {
    return fallback;
  }
  return value->as_bool();
}

void strip_utf8_bom(std::string &text) {
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
      static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
    text.erase(0, 3);
  }
}

Json load_metadata() {
  std::string error;
  auto json = read_json_file(metadata_path(), &error);
  if (json && json->is_object()) {
    return *json;
  }
  return Json(Json::object_type{});
}

void save_metadata(const Json &metadata) {
  std::string error;
  if (!write_json_file(metadata_path(), metadata, &error)) {
    throw std::runtime_error("cannot write hook metadata: " + error);
  }
}

bool command_has_marker(const std::string &text, const std::string &provider) {
  return text.find("cat-light-" + provider + "-hook") != std::string::npos ||
         text.find("cat-light event --provider " + provider) != std::string::npos;
}

std::filesystem::path backup_file(const std::filesystem::path &path) {
  if (!file_exists(path)) {
    return {};
  }
  std::filesystem::create_directories(path.parent_path());
  const std::string base = path.filename().string() + ".cat-light-backup-" + safe_timestamp();
  for (int i = 0; i < 100; ++i) {
    auto candidate = path.parent_path() / (i == 0 ? base : base + "-" + std::to_string(i));
    std::error_code ec;
    if (!std::filesystem::exists(candidate, ec)) {
      std::filesystem::copy_file(path, candidate, std::filesystem::copy_options::none, ec);
      if (ec) {
        throw std::runtime_error("cannot back up " + path.string() + ": " + ec.message());
      }
      return candidate;
    }
  }
  throw std::runtime_error("cannot choose backup path for " + path.string());
}

void make_executable_if_needed(const std::filesystem::path &path) {
#ifndef _WIN32
  chmod(path.string().c_str(), S_IRUSR | S_IWUSR | S_IXUSR);
#else
  (void)path;
#endif
}

std::vector<std::string> parse_toml_string_array(const std::string &value) {
  std::vector<std::string> out;
  bool in_string = false;
  bool escape = false;
  std::string current;
  for (char c : value) {
    if (!in_string) {
      if (c == '"') {
        in_string = true;
        current.clear();
      }
      continue;
    }
    if (escape) {
      switch (c) {
      case 'n':
        current.push_back('\n');
        break;
      case 'r':
        current.push_back('\r');
        break;
      case 't':
        current.push_back('\t');
        break;
      default:
        current.push_back(c);
        break;
      }
      escape = false;
      continue;
    }
    if (c == '\\') {
      escape = true;
      continue;
    }
    if (c == '"') {
      in_string = false;
      out.push_back(current);
      continue;
    }
    current.push_back(c);
  }
  return out;
}

size_t toml_assignment_end(const std::string &text, size_t value_begin, size_t first_line_end) {
  bool in_string = false;
  bool escape = false;
  bool saw_array = false;
  int bracket_depth = 0;
  for (size_t i = value_begin; i < text.size(); ++i) {
    char c = text[i];
    if (in_string) {
      if (escape) {
        escape = false;
      } else if (c == '\\') {
        escape = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }
    if (c == '"') {
      in_string = true;
      continue;
    }
    if (c == '[') {
      saw_array = true;
      ++bracket_depth;
      continue;
    }
    if (c == ']') {
      --bracket_depth;
      if (saw_array && bracket_depth <= 0) {
        size_t end = i + 1;
        while (end < text.size() && text[end] != '\n') {
          ++end;
        }
        if (end < text.size()) {
          ++end;
        }
        return end;
      }
    }
  }
  if (first_line_end < text.size()) {
    return first_line_end + 1;
  }
  return text.size();
}

NotifyAssignment find_top_level_notify(const std::string &text) {
  size_t line_begin = 0;
  bool in_table = false;
  while (line_begin <= text.size()) {
    size_t line_end = text.find('\n', line_begin);
    if (line_end == std::string::npos) {
      line_end = text.size();
    }
    std::string line = text.substr(line_begin, line_end - line_begin);
    std::string trimmed = trim(line);
    if (!trimmed.empty() && trimmed[0] == '[') {
      in_table = true;
    }
    if (!in_table && !trimmed.empty() && trimmed[0] != '#') {
      size_t pos = 0;
      while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {
        ++pos;
      }
      if (line.compare(pos, 6, "notify") == 0) {
        size_t after_key = pos + 6;
        while (after_key < line.size() && std::isspace(static_cast<unsigned char>(line[after_key]))) {
          ++after_key;
        }
        if (after_key < line.size() && line[after_key] == '=') {
          size_t value_begin = line_begin + after_key + 1;
          size_t end = toml_assignment_end(text, value_begin, line_end);
          NotifyAssignment assignment;
          assignment.found = true;
          assignment.begin = line_begin;
          assignment.end = end;
          assignment.text = text.substr(assignment.begin, assignment.end - assignment.begin);
          assignment.argv = parse_toml_string_array(assignment.text.substr(assignment.text.find('=') + 1));
          return assignment;
        }
      }
    }
    if (line_end == text.size()) {
      break;
    }
    line_begin = line_end + 1;
  }
  return {};
}

std::string ps_array(const std::vector<std::string> &values) {
  if (values.empty()) {
    return "@()";
  }
  std::vector<std::string> quoted;
  for (const auto &value : values) {
    quoted.push_back(ps_quote(value));
  }
  return "@(" + join_strings(quoted, ", ") + ")";
}

std::string powershell_claude_script(const std::filesystem::path &exe_path) {
  std::ostringstream out;
  out << R"($ErrorActionPreference = 'SilentlyContinue'
$CatLight = )" << ps_quote(absolute_exe(exe_path)) << R"(
$RawInput = [Console]::In.ReadToEnd()
if ([string]::IsNullOrWhiteSpace($RawInput)) { $RawInput = '{}' }
try { $Hook = $RawInput | ConvertFrom-Json -ErrorAction Stop } catch { $Hook = [pscustomobject]@{} }

function First-Text([object[]]$Values, [string]$Fallback = '') {
  foreach ($Value in $Values) {
    if ($null -ne $Value) {
      $Text = [string]$Value
      if (-not [string]::IsNullOrWhiteSpace($Text)) { return $Text }
    }
  }
  return $Fallback
}

$EventName = First-Text @($Hook.hook_event_name, $Hook.hookEvent, $Hook.event, $env:CLAUDE_HOOK_EVENT_NAME) 'Hook'
$ToolName = First-Text @($Hook.tool_name, $Hook.hookName, $Hook.toolName, $Hook.matcher) ''
$SessionId = First-Text @($Hook.session_id, $Hook.sessionId, $Hook.conversation_id, $Hook.promptId) 'claude-hook'
$ProjectPath = First-Text @($Hook.cwd, $Hook.project_path, $Hook.workspace, (Get-Location).Path) ''

$State = switch -Regex ($EventName) {
  'UserPromptSubmit|SessionStart' { 'starting'; break }
  'PreToolUse|PostToolUse' { 'working'; break }
  'Notification' { 'waiting'; break }
  'Stop|SubagentStop|SessionEnd' { 'complete'; break }
  default { 'working' }
}

$Detail = if ($ToolName) { "$EventName $ToolName" } else { $EventName }
$Payload = [ordered]@{
  provider = 'claude'
  source = 'hook'
  session_id = $SessionId
  project_path = $ProjectPath
  state = $State
  detail = $Detail
  tool_name = $ToolName
  raw_kind = $EventName
}
$Payload | ConvertTo-Json -Compress | & $CatLight event --stdin | Out-Null
exit 0
)";
  return out.str();
}

std::string powershell_codex_script(const std::filesystem::path &exe_path,
                                    const std::vector<std::string> &original_notify) {
  std::ostringstream out;
  out << R"(param(
  [Parameter(ValueFromRemainingArguments = $true)]
  [string[]]$NotifyArgs
)
$ErrorActionPreference = 'SilentlyContinue'
$CatLight = )" << ps_quote(absolute_exe(exe_path)) << R"(
$OriginalNotify = )" << ps_array(original_notify) << R"(
$RawInput = [Console]::In.ReadToEnd()
$Text = (($NotifyArgs -join ' ') + ' ' + $RawInput).Trim()
$Obj = $null
if (-not [string]::IsNullOrWhiteSpace($Text)) {
  try { $Obj = $Text | ConvertFrom-Json -ErrorAction Stop } catch { $Obj = $null }
}

function First-Text([object[]]$Values, [string]$Fallback = '') {
  foreach ($Value in $Values) {
    if ($null -ne $Value) {
      $TextValue = [string]$Value
      if (-not [string]::IsNullOrWhiteSpace($TextValue)) { return $TextValue }
    }
  }
  return $Fallback
}

$Kind = First-Text @($Obj.type, $Obj.event, $Obj.kind, $Obj.notification_type) 'notify'
$SessionId = First-Text @($Obj.session_id, $Obj.sessionId, $Obj.conversation_id, $env:CODEX_SESSION_ID) 'codex-notify'
$ProjectPath = First-Text @($Obj.cwd, $Obj.project_path, $Obj.workspace, (Get-Location).Path) ''

$State = 'working'
if ($Text -match 'approval|permission|confirm|blocked') { $State = 'waiting' }
elseif ($Text -match 'complete|completed|done|finished|final') { $State = 'complete' }
elseif ($Text -match 'error|failed|aborted|panic') { $State = 'error' }
elseif ($Text -match 'reason|think') { $State = 'thinking' }

$Detail = if ([string]::IsNullOrWhiteSpace($Kind)) { 'Codex notify' } else { "Codex $Kind" }
$Payload = [ordered]@{
  provider = 'codex'
  source = 'hook'
  session_id = $SessionId
  project_path = $ProjectPath
  state = $State
  detail = $Detail
  raw_kind = $Kind
}
$Payload | ConvertTo-Json -Compress | & $CatLight event --stdin | Out-Null

if ($OriginalNotify.Count -gt 0) {
  try {
    $OriginalExe = $OriginalNotify[0]
    $OriginalArgs = @()
    if ($OriginalNotify.Count -gt 1) { $OriginalArgs = $OriginalNotify[1..($OriginalNotify.Count - 1)] }
    if ([string]::IsNullOrWhiteSpace($RawInput)) {
      & $OriginalExe @OriginalArgs @NotifyArgs | Out-Null
    } else {
      $RawInput | & $OriginalExe @OriginalArgs @NotifyArgs | Out-Null
    }
  } catch {}
}
exit 0
)";
  return out.str();
}

std::string sh_script(const std::filesystem::path &exe_path, const std::string &provider) {
  std::ostringstream out;
  out << "#!/usr/bin/env sh\n";
  out << "CAT_LIGHT=" << sh_quote(absolute_exe(exe_path)) << "\n";
  out << "RAW=$(cat)\n";
  out << "if [ -n \"$RAW\" ]; then\n";
  out << "  printf '%s\\n' \"$RAW\" | \"$CAT_LIGHT\" event --stdin >/dev/null 2>&1 && exit 0\n";
  out << "fi\n";
  out << "printf '%s\\n' " << sh_quote("{\"provider\":\"" + provider +
                                            "\",\"source\":\"hook\",\"session_id\":\"" + provider +
                                            "-hook\",\"state\":\"working\",\"detail\":\"Hook event\"}")
      << " | \"$CAT_LIGHT\" event --stdin >/dev/null 2>&1\n";
  out << "exit 0\n";
  return out.str();
}

std::string script_for(const std::filesystem::path &exe_path,
                       const std::string &provider,
                       const std::string &shell,
                       const std::vector<std::string> &original_codex_notify = {}) {
  if (shell == "sh") {
    return sh_script(exe_path, provider);
  }
  if (provider == "claude") {
    return powershell_claude_script(exe_path);
  }
  return powershell_codex_script(exe_path, original_codex_notify);
}

std::string shell_command_name(const std::string &shell) {
  if (shell == "pwsh") {
    return "pwsh";
  }
  if (shell == "sh") {
    return "sh";
  }
  return "powershell.exe";
}

std::string hook_command_string(const std::filesystem::path &path, const std::string &shell) {
  if (shell == "sh") {
    return "sh \"" + path.string() + "\"";
  }
  return shell_command_name(shell) + " -NoProfile -ExecutionPolicy Bypass -File \"" + path.string() + "\"";
}

std::string hook_command_for_json(const std::filesystem::path &path, const std::string &shell) {
  return json_quote(hook_command_string(path, shell));
}

std::string codex_notify_toml(const std::filesystem::path &path, const std::string &shell) {
  std::vector<std::string> parts;
  if (shell == "sh") {
    parts = {"notify = [", toml_quote("sh"), ", ", toml_quote(path.string()), "]"};
  } else {
    parts = {"notify = [", toml_quote(shell_command_name(shell)), ", ", toml_quote("-NoProfile"), ", ",
             toml_quote("-ExecutionPolicy"), ", ", toml_quote("Bypass"), ", ", toml_quote("-File"), ", ",
             toml_quote(path.string()), "]"};
  }
  return join_strings(parts, "");
}

std::vector<std::string> claude_hook_events() {
  return {"UserPromptSubmit", "PreToolUse", "PostToolUse", "Notification", "Stop", "SessionEnd"};
}

bool is_catlight_command_object(const Json &value, const std::string &provider) {
  if (!value.is_object()) {
    return false;
  }
  const std::string type = json_string(value.get("type"));
  const std::string command = json_string(value.get("command"));
  return type == "command" && command_has_marker(command, provider);
}

bool clean_claude_hook_item(const Json &item, Json &cleaned, bool &removed) {
  if (is_catlight_command_object(item, "claude")) {
    removed = true;
    return false;
  }
  if (!item.is_object()) {
    cleaned = item;
    return true;
  }

  cleaned = item;
  Json *hooks = cleaned.get("hooks");
  if (hooks && hooks->is_array()) {
    Json::array_type next_hooks;
    for (const auto &hook : hooks->array()) {
      Json next;
      if (clean_claude_hook_item(hook, next, removed)) {
        next_hooks.push_back(next);
      }
    }
    cleaned["hooks"] = Json(std::move(next_hooks));
    if (cleaned.get("hooks")->array().empty()) {
      return false;
    }
  }
  return true;
}

bool remove_claude_hooks(Json &settings) {
  Json *hooks = settings.get("hooks");
  if (!hooks || !hooks->is_object()) {
    return false;
  }
  bool removed = false;
  Json::object_type cleaned_hooks;
  for (const auto &entry : hooks->object()) {
    if (!entry.second.is_array()) {
      cleaned_hooks[entry.first] = entry.second;
      continue;
    }
    Json::array_type cleaned_event;
    for (const auto &item : entry.second.array()) {
      Json cleaned_item;
      if (clean_claude_hook_item(item, cleaned_item, removed)) {
        cleaned_event.push_back(cleaned_item);
      }
    }
    if (!cleaned_event.empty()) {
      cleaned_hooks[entry.first] = Json(std::move(cleaned_event));
    }
  }
  settings["hooks"] = Json(std::move(cleaned_hooks));
  return removed;
}

void add_claude_hooks(Json &settings, const std::string &command) {
  Json &hooks = settings["hooks"];
  hooks.object();
  for (const auto &event : claude_hook_events()) {
    Json &event_hooks = hooks[event];
    event_hooks.array();
    Json::object_type command_hook;
    command_hook["type"] = "command";
    command_hook["command"] = command;

    Json::array_type nested;
    nested.push_back(Json(std::move(command_hook)));

    Json::object_type group;
    group["hooks"] = Json(std::move(nested));
    event_hooks.array().push_back(Json(std::move(group)));
  }
}

bool claude_config_has_hook(const Json &settings) {
  const Json *hooks = settings.get("hooks");
  if (!hooks || !hooks->is_object()) {
    return false;
  }
  for (const auto &entry : hooks->object()) {
    if (!entry.second.is_array()) {
      continue;
    }
    for (const auto &item : entry.second.array()) {
      bool removed = false;
      Json cleaned;
      if (!clean_claude_hook_item(item, cleaned, removed) && removed) {
        return true;
      }
      if (removed) {
        return true;
      }
    }
  }
  return false;
}

std::string install_claude(const Options &options,
                           const std::filesystem::path &exe_path,
                           const std::string &shell,
                           Json &metadata) {
  std::ostringstream out;
  const auto settings_path = claude_settings_path();
  const auto script = script_path("claude", shell);
  const std::string command = hook_command_string(script, shell);
  out << "Claude\n";
  out << "  settings: " << settings_path.string() << "\n";
  out << "  script:   " << script.string() << "\n";

  if (options.dry_run) {
    out << "  action:   would write helper script and append Claude hooks\n";
    return out.str();
  }

  write_text_file(script, script_for(exe_path, "claude", shell));
  make_executable_if_needed(script);

  Json settings(Json::object_type{});
  if (file_exists(settings_path)) {
    std::string error;
    auto parsed = read_json_file(settings_path, &error);
    if (!parsed || !parsed->is_object()) {
      throw std::runtime_error("cannot parse Claude settings: " + error);
    }
    settings = *parsed;
  }

  remove_claude_hooks(settings);
  add_claude_hooks(settings, command);

  std::filesystem::path backup = backup_file(settings_path);
  std::string error;
  if (!write_json_file(settings_path, settings, &error)) {
    throw std::runtime_error("cannot write Claude settings: " + error);
  }

  Json::object_type entry;
  entry["installed"] = true;
  entry["settings_path"] = settings_path.string();
  entry["script_path"] = script.string();
  entry["shell"] = shell;
  entry["command"] = command;
  entry["installed_at"] = format_iso_utc(Clock::now());
  if (!backup.empty()) {
    entry["backup_path"] = backup.string();
  }
  metadata["claude"] = Json(std::move(entry));

  out << "  action:   installed";
  if (!backup.empty()) {
    out << " (backup " << backup.string() << ")";
  }
  out << "\n";
  return out.str();
}

std::vector<std::string> original_codex_notify_from_metadata(const Json &metadata) {
  const Json *codex = metadata.get("codex");
  if (!codex || !codex->is_object() || !json_bool(codex->get("original_had_notify"))) {
    return {};
  }
  return parse_toml_string_array(json_string(codex->get("original_notify")));
}

std::string install_codex(const Options &options,
                          const std::filesystem::path &exe_path,
                          const std::string &shell,
                          Json &metadata) {
  std::ostringstream out;
  const auto config_path = codex_config_path();
  const auto script = script_path("codex", shell);
  std::string current = file_exists(config_path) ? read_text_file(config_path) : "";
  strip_utf8_bom(current);
  NotifyAssignment notify = find_top_level_notify(current);
  const bool current_is_catlight = notify.found && command_has_marker(notify.text, "codex");

  bool original_had_notify = false;
  std::string original_notify;
  if (const Json *codex = metadata.get("codex"); codex && codex->is_object()) {
    original_had_notify = json_bool(codex->get("original_had_notify"));
    original_notify = json_string(codex->get("original_notify"));
  }
  if (!current_is_catlight) {
    original_had_notify = notify.found;
    original_notify = notify.found ? notify.text : "";
  }

  std::vector<std::string> original_argv = original_had_notify ? parse_toml_string_array(original_notify) : std::vector<std::string>{};
  if (!original_argv.empty() && command_has_marker(join_strings(original_argv, " "), "codex")) {
    original_argv.clear();
  }

  out << "Codex\n";
  out << "  config:   " << config_path.string() << "\n";
  out << "  script:   " << script.string() << "\n";

  if (options.dry_run) {
    out << "  action:   would write helper script and replace top-level notify";
    if (original_had_notify) {
      out << " while preserving original notify for uninstall";
    }
    out << "\n";
    return out.str();
  }

  write_text_file(script, script_for(exe_path, "codex", shell, original_argv));
  make_executable_if_needed(script);

  const std::string replacement = codex_notify_toml(script, shell) + "\n";
  std::string next;
  if (notify.found) {
    next = current.substr(0, notify.begin) + replacement + current.substr(notify.end);
  } else {
    next = replacement + (current.empty() ? "" : "\n" + current);
  }

  std::filesystem::path backup;
  if (next != current) {
    backup = backup_file(config_path);
    write_text_file(config_path, next);
  }

  Json::object_type entry;
  entry["installed"] = true;
  entry["config_path"] = config_path.string();
  entry["script_path"] = script.string();
  entry["shell"] = shell;
  entry["installed_at"] = format_iso_utc(Clock::now());
  entry["original_had_notify"] = original_had_notify;
  entry["original_notify"] = original_notify;
  if (!backup.empty()) {
    entry["backup_path"] = backup.string();
  }
  metadata["codex"] = Json(std::move(entry));

  out << "  action:   installed";
  if (original_had_notify) {
    out << " (original notify saved";
    if (!original_argv.empty() && shell != "sh") {
      out << " and chained";
    }
    out << ")";
  }
  if (!backup.empty()) {
    out << "\n  backup:   " << backup.string();
  }
  out << "\n";
  return out.str();
}

void remove_generated_script(const std::string &provider, const Json &metadata) {
  std::vector<std::filesystem::path> candidates;
  if (const Json *entry = metadata.get(provider); entry && entry->is_object()) {
    std::string saved = json_string(entry->get("script_path"));
    if (!saved.empty()) {
      candidates.push_back(saved);
    }
  }
  candidates.push_back(script_path(provider, "powershell"));
  candidates.push_back(script_path(provider, "pwsh"));
  candidates.push_back(script_path(provider, "sh"));
  for (const auto &path : candidates) {
    std::error_code ec;
    if (path.parent_path() == hook_dir()) {
      std::filesystem::remove(path, ec);
    }
  }
}

std::string uninstall_claude(const Options &options, Json &metadata) {
  std::ostringstream out;
  const auto settings_path = claude_settings_path();
  out << "Claude\n";
  out << "  settings: " << settings_path.string() << "\n";
  if (options.dry_run) {
    out << "  action:   would remove cat-light Claude hook commands\n";
    return out.str();
  }
  bool changed = false;
  if (file_exists(settings_path)) {
    std::string error;
    auto parsed = read_json_file(settings_path, &error);
    if (!parsed || !parsed->is_object()) {
      throw std::runtime_error("cannot parse Claude settings: " + error);
    }
    Json settings = *parsed;
    changed = remove_claude_hooks(settings);
    if (changed) {
      auto backup = backup_file(settings_path);
      if (!write_json_file(settings_path, settings, &error)) {
        throw std::runtime_error("cannot write Claude settings: " + error);
      }
      out << "  backup:   " << backup.string() << "\n";
    }
  }
  remove_generated_script("claude", metadata);
  metadata.object().erase("claude");
  out << "  action:   " << (changed ? "uninstalled" : "no hook found") << "\n";
  return out.str();
}

std::string uninstall_codex(const Options &options, Json &metadata) {
  std::ostringstream out;
  const auto config_path = codex_config_path();
  out << "Codex\n";
  out << "  config:   " << config_path.string() << "\n";
  if (options.dry_run) {
    out << "  action:   would remove cat-light notify and restore saved original notify if present\n";
    return out.str();
  }
  bool changed = false;
  if (file_exists(config_path)) {
    std::string current = read_text_file(config_path);
    strip_utf8_bom(current);
    NotifyAssignment notify = find_top_level_notify(current);
    if (notify.found && command_has_marker(notify.text, "codex")) {
      bool original_had_notify = false;
      std::string original_notify;
      if (const Json *codex = metadata.get("codex"); codex && codex->is_object()) {
        original_had_notify = json_bool(codex->get("original_had_notify"));
        original_notify = json_string(codex->get("original_notify"));
      }
      std::string replacement = original_had_notify ? original_notify : "";
      std::string next = current.substr(0, notify.begin) + replacement + current.substr(notify.end);
      auto backup = backup_file(config_path);
      write_text_file(config_path, next);
      out << "  backup:   " << backup.string() << "\n";
      changed = true;
    }
  }
  remove_generated_script("codex", metadata);
  metadata.object().erase("codex");
  out << "  action:   " << (changed ? "uninstalled" : "no hook found") << "\n";
  return out.str();
}

std::string install_notes(const Options &options, const std::string &shell) {
  std::ostringstream out;
  if (wants_provider(options, "claude")) {
    auto path = script_path("claude", shell);
    out << "\nClaude command:\n";
    out << hook_command_for_json(path, shell) << "\n";
  }
  if (wants_provider(options, "codex")) {
    auto path = script_path("codex", shell);
    out << "\nCodex notify:\n";
    out << codex_notify_toml(path, shell) << "\n";
  }
  return out.str();
}

} // namespace

std::string render_hook_script(const Options &options, const std::filesystem::path &exe_path) {
  const std::string shell = default_shell(options);
  std::ostringstream out;
  if (wants_provider(options, "claude")) {
    out << script_for(exe_path, "claude", shell);
  }
  if (wants_provider(options, "codex")) {
    if (wants_provider(options, "claude")) {
      out << "\n";
    }
    out << script_for(exe_path, "codex", shell, original_codex_notify_from_metadata(load_metadata()));
  }
  return out.str();
}

std::string install_hook_scripts(const Options &options, const std::filesystem::path &exe_path) {
  const std::string shell = default_shell(options);
  Json metadata = load_metadata();
  std::ostringstream out;
  out << (options.dry_run ? "Hook install dry run\n" : "Hook install\n");
  if (!options.dry_run) {
    std::filesystem::create_directories(hook_dir());
  }
  if (wants_provider(options, "claude")) {
    out << install_claude(options, exe_path, shell, metadata);
  }
  if (wants_provider(options, "codex")) {
    out << install_codex(options, exe_path, shell, metadata);
  }
  if (!options.dry_run) {
    save_metadata(metadata);
  }
  out << install_notes(options, shell);
  return out.str();
}

std::string uninstall_hooks(const Options &options) {
  Json metadata = load_metadata();
  std::ostringstream out;
  out << (options.dry_run ? "Hook uninstall dry run\n" : "Hook uninstall\n");
  if (wants_provider(options, "claude")) {
    out << uninstall_claude(options, metadata);
  }
  if (wants_provider(options, "codex")) {
    out << uninstall_codex(options, metadata);
  }
  if (!options.dry_run) {
    save_metadata(metadata);
  }
  return out.str();
}

std::string render_hook_status(const Options &options) {
  std::ostringstream out;
  if (wants_provider(options, "claude")) {
    bool installed = false;
    std::string error;
    auto settings = read_json_file(claude_settings_path(), &error);
    if (settings && settings->is_object()) {
      installed = claude_config_has_hook(*settings);
    }
    out << "Claude: " << (installed ? "installed" : "not installed") << "\n";
    out << "  settings: " << claude_settings_path().string() << "\n";
    out << "  script: " << script_path("claude", default_shell(options)).string() << "\n";
  }
  if (wants_provider(options, "codex")) {
    bool installed = false;
    if (file_exists(codex_config_path())) {
      std::string config = read_text_file(codex_config_path());
      strip_utf8_bom(config);
      auto notify = find_top_level_notify(config);
      installed = notify.found && command_has_marker(notify.text, "codex");
    }
    out << "Codex: " << (installed ? "installed" : "not installed") << "\n";
    out << "  config: " << codex_config_path().string() << "\n";
    out << "  script: " << script_path("codex", default_shell(options)).string() << "\n";
  }
  return out.str();
}

} // namespace catlight
