#include "app.hpp"

#include "agent.hpp"
#include "history_store.hpp"
#include "http_client.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>

namespace catlight {
namespace {

#ifdef _WIN32
constexpr const char *kPathSeparator = ";";
#else
constexpr const char *kPathSeparator = ":";
#endif

std::vector<std::string> split_path_list(const std::string &value) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= value.size()) {
    size_t pos = value.find(kPathSeparator, start);
    std::string item = pos == std::string::npos ? value.substr(start) : value.substr(start, pos - start);
    item = trim(item);
    if (!item.empty()) {
      out.push_back(item);
    }
    if (pos == std::string::npos) {
      break;
    }
    start = pos + 1;
  }
  return out;
}

std::vector<std::string> executable_suffixes() {
#ifdef _WIN32
  if (auto pathext = env_var("PATHEXT")) {
    auto suffixes = split_path_list(*pathext);
    if (!suffixes.empty()) {
      return suffixes;
    }
  }
  return {".exe", ".cmd", ".bat"};
#else
  return {""};
#endif
}

bool is_date_only(const std::string &value) {
  if (value.size() != 10 || value[4] != '-' || value[7] != '-') {
    return false;
  }
  for (size_t i = 0; i < value.size(); ++i) {
    if (i == 4 || i == 7) {
      continue;
    }
    if (!std::isdigit(static_cast<unsigned char>(value[i]))) {
      return false;
    }
  }
  return true;
}

std::optional<TimePoint> parse_option_time(const std::string &value, bool end_of_day) {
  auto parsed = parse_time_text(value);
  if (!parsed) {
    return std::nullopt;
  }
  if (end_of_day && is_date_only(trim(value))) {
    return *parsed + std::chrono::hours(24);
  }
  return parsed;
}

std::optional<std::filesystem::path> find_command_on_path(const std::string &name) {
  auto path_env = env_var("PATH");
  if (!path_env) {
    return std::nullopt;
  }
  std::vector<std::string> suffixes = executable_suffixes();
  bool has_suffix = std::filesystem::path(name).extension().string().size() > 0;
  for (const auto &dir : split_path_list(*path_env)) {
    if (has_suffix) {
      std::filesystem::path candidate = std::filesystem::path(dir) / name;
      if (file_exists(candidate)) {
        return candidate;
      }
    } else {
      for (const auto &suffix : suffixes) {
        std::filesystem::path candidate = std::filesystem::path(dir) / (name + suffix);
        if (file_exists(candidate)) {
          return candidate;
        }
      }
    }
  }
  return std::nullopt;
}

DoctorCheck path_check(const std::string &provider,
                       const std::string &display,
                       const std::string &label,
                       const std::filesystem::path &path,
                       const std::string &missing_message = "") {
  DoctorCheck check;
  check.provider = provider;
  check.display_name = display;
  check.credential_label = label;
  check.credential_path = path;
  check.credential_ok = !path.empty() && file_exists(path);
  check.cache_ok = true;
  if (!check.credential_ok) {
    check.message = missing_message;
  }
  return check;
}

DoctorCheck command_check(const std::string &provider, const std::string &display, const std::string &command) {
  DoctorCheck check;
  check.provider = provider;
  check.display_name = display;
  check.credential_label = "command";
  if (auto found = find_command_on_path(command)) {
    check.credential_path = *found;
    check.credential_ok = true;
  }
  check.cache_ok = true;
  if (!check.credential_ok) {
    check.message = command + " is not on PATH";
  }
  return check;
}

DoctorCheck hook_event_check(const std::string &provider, const std::string &display) {
  DoctorCheck check;
  check.provider = provider + "-hook-events";
  check.display_name = display;
  check.credential_label = "recent event";
  check.credential_path = event_log_path();
  check.cache_ok = true;

  std::string error;
  auto events = read_agent_events(&error);
  std::optional<AgentEvent> latest;
  for (const auto &event : events) {
    if (event.provider == provider && event.source == "hook") {
      if (!latest || event.timestamp > latest->timestamp) {
        latest = event;
      }
    }
  }
  if (!latest) {
    check.credential_ok = false;
    check.message = "no hook event found yet";
    return check;
  }
  auto age = std::chrono::duration_cast<std::chrono::hours>(Clock::now() - latest->timestamp);
  check.credential_ok = age <= std::chrono::hours(24);
  check.message = "last hook event " + human_age(latest->timestamp, Clock::now()) + " ago";
  if (!check.credential_ok) {
    check.message += " (older than 24h)";
  }
  return check;
}

} // namespace

Snapshot collect_snapshot(const Options &options) {
  Snapshot snapshot;
  snapshot.generated_at = Clock::now();
  if (provider_selected(options, "codex")) {
    snapshot.statuses.push_back(fetch_codex_status(options));
  }
  if (provider_selected(options, "claude")) {
    snapshot.statuses.push_back(fetch_claude_status(options));
  }
  return snapshot;
}

std::vector<DoctorCheck> run_doctor(const Options &options) {
  std::vector<DoctorCheck> checks;
  if (provider_selected(options, "codex")) {
    DoctorCheck check;
    check.provider = "codex";
    check.display_name = "Codex";
    check.credential_path = codex_auth_path();
    check.cache_path = provider_cache_path("codex");
    check.credential_ok = file_exists(check.credential_path);
    check.cache_ok = file_exists(check.cache_path);
    if (!check.credential_ok) {
      check.message = "missing ~/.codex/auth.json; run codex login";
    }
    checks.push_back(check);
    checks.push_back(hook_event_check("codex", "Codex hook events"));
  }
  if (provider_selected(options, "claude")) {
    DoctorCheck check;
    check.provider = "claude";
    check.display_name = "Claude";
    check.credential_path = claude_credentials_path();
    check.cache_path = provider_cache_path("claude");
    check.credential_ok = file_exists(check.credential_path);
    check.cache_ok = file_exists(check.cache_path);
    if (!check.credential_ok) {
      check.message = "missing ~/.claude/.credentials.json; run claude login";
    }
    checks.push_back(check);
    checks.push_back(hook_event_check("claude", "Claude hook events"));
  }
  DoctorCheck curl;
  curl.provider = "curl";
  curl.display_name = "curl";
  curl.credential_label = "command";
  curl.credential_ok = HttpClient::curl_available();
  curl.cache_ok = true;
  if (!curl.credential_ok) {
    curl.message = "curl is required for network requests";
  }
  checks.push_back(curl);

  auto cmake = command_check("cmake", "CMake in PATH", "cmake");
#ifdef _WIN32
  const std::filesystem::path vs_cmake =
      "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe";
  if (!cmake.credential_ok && file_exists(vs_cmake)) {
    cmake.message += "; Visual Studio CMake is available at " + vs_cmake.string();
  }
  checks.push_back(cmake);
  checks.push_back(path_check("vs-cmake", "Visual Studio CMake", "path", vs_cmake,
                              "Visual Studio bundled CMake was not found"));
#else
  checks.push_back(cmake);
#endif

  checks.push_back(command_check("ninja", "Ninja in PATH", "ninja"));
  checks.push_back(command_check("sqlite3", "sqlite3 command", "sqlite3"));

  DoctorCheck sqlite_backend;
  sqlite_backend.provider = "sqlite-backend";
  sqlite_backend.display_name = "SQLite history backend";
  sqlite_backend.credential_label = "compiled";
  sqlite_backend.credential_ok = sqlite_history_available();
  sqlite_backend.cache_label = "database";
  sqlite_backend.cache_path = history_database_path();
  sqlite_backend.cache_ok = true;
  if (!sqlite_backend.credential_ok) {
    sqlite_backend.message = "not compiled; JSONL history backend is available. Rebuild with CAT_LIGHT_ENABLE_SQLITE=ON after installing SQLite3 development files";
  }
#ifdef CAT_LIGHT_BUNDLED_SQLITE
  if (sqlite_backend.credential_ok) {
    sqlite_backend.message = "compiled with bundled SQLite";
  }
#endif
  checks.push_back(sqlite_backend);

#ifdef _WIN32
#ifndef CAT_LIGHT_BUNDLED_SQLITE
  const std::filesystem::path msys2_sqlite_h = "C:\\msys64\\ucrt64\\include\\sqlite3.h";
  const std::filesystem::path msys2_sqlite_lib = "C:\\msys64\\ucrt64\\lib\\libsqlite3.dll.a";
  DoctorCheck msys2_sqlite;
  msys2_sqlite.provider = "msys2-sqlite";
  msys2_sqlite.display_name = "MSYS2 UCRT64 SQLite";
  msys2_sqlite.credential_label = "include";
  msys2_sqlite.credential_path = msys2_sqlite_h;
  msys2_sqlite.credential_ok = file_exists(msys2_sqlite_h);
  msys2_sqlite.cache_label = "library";
  msys2_sqlite.cache_path = msys2_sqlite_lib;
  msys2_sqlite.cache_ok = file_exists(msys2_sqlite_lib);
  if (!msys2_sqlite.credential_ok || !msys2_sqlite.cache_ok) {
    msys2_sqlite.message = "install with MSYS2: pacman -S --needed mingw-w64-ucrt-x86_64-sqlite";
  }
  checks.push_back(msys2_sqlite);
#endif
#endif
  return checks;
}

std::string provider_state_text(ProviderState state) {
  switch (state) {
  case ProviderState::Ok:
    return "ok";
  case ProviderState::MissingAuth:
    return "missing_auth";
  case ProviderState::AuthError:
    return "auth_error";
  case ProviderState::NetworkError:
    return "network_error";
  case ProviderState::ParseError:
    return "parse_error";
  case ProviderState::Stale:
    return "stale";
  case ProviderState::Offline:
    return "offline";
  }
  return "unknown";
}

std::string severity_for_percent(int percent) {
  percent = clamp_percent(percent);
  if (percent >= 90) {
    return "critical";
  }
  if (percent >= 75) {
    return "high";
  }
  if (percent >= 50) {
    return "mid";
  }
  return "low";
}

int max_percent(const ProviderStatus &status) {
  int value = 0;
  for (const auto &window : status.windows) {
    value = std::max(value, window.used_percent);
  }
  if (status.credits) {
    value = std::max(value, status.credits->extra_percent);
  }
  return clamp_percent(value);
}

std::string overall_severity(const Snapshot &snapshot) {
  bool has_error = false;
  int value = 0;
  for (const auto &status : snapshot.statuses) {
    value = std::max(value, max_percent(status));
    if (status.state != ProviderState::Ok && status.state != ProviderState::Stale && status.state != ProviderState::Offline) {
      has_error = true;
    }
  }
  if (has_error && value < 90) {
    return "critical";
  }
  return severity_for_percent(value);
}

void enrich_window(UsageWindow &window, TimePoint now) {
  window.used_percent = clamp_percent(window.used_percent);
  window.remaining_percent = 100 - window.used_percent;
  window.elapsed_percent = elapsed_percent(window.reset_at, window.window, now);
  window.pace_delta = window.used_percent - window.elapsed_percent;
  window.pace = pace_label(window.pace_delta);
}

bool provider_selected(const Options &options, const std::string &provider) {
  std::string selected = to_lower(options.provider);
  return selected == "all" || selected.empty() || selected == provider;
}

Options parse_options(int argc, char **argv, std::string &command, std::string &error) {
  Options options;
  if (argc >= 2) {
    command = argv[1];
  } else {
    command = "status";
  }

  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    auto require_value = [&](const std::string &name) -> std::string {
      if (i + 1 >= argc) {
        error = "missing value for " + name;
        return "";
      }
      return argv[++i];
    };
    if (arg == "--provider" || arg == "-p") {
      options.provider = to_lower(require_value(arg));
    } else if (arg == "--offline") {
      options.offline = true;
    } else if (arg == "--refresh") {
      options.refresh = true;
    } else if (arg == "--no-token-refresh") {
      options.token_refresh = false;
    } else if (arg == "--timeout") {
      options.timeout_seconds = std::max(1, std::stoi(require_value(arg)));
    } else if (arg == "--max-sessions") {
      options.max_sessions = std::max(1, std::stoi(require_value(arg)));
    } else if (arg == "--cache-ttl") {
      options.cache_ttl = std::chrono::seconds(std::max(0, std::stoi(require_value(arg))));
    } else if (arg == "--addr") {
      options.addr = require_value(arg);
    } else if (arg == "--shell") {
      options.hook_shell = to_lower(require_value(arg));
    } else if (arg == "--storage") {
      options.storage_backend = to_lower(require_value(arg));
    } else if (arg == "--since") {
      const std::string value = require_value(arg);
      auto parsed = parse_option_time(value, false);
      if (!parsed) {
        error = "invalid --since, expected YYYY-MM-DD, RFC3339, or epoch seconds";
      } else {
        options.history_since = *parsed;
      }
    } else if (arg == "--until") {
      const std::string value = require_value(arg);
      auto parsed = parse_option_time(value, true);
      if (!parsed) {
        error = "invalid --until, expected YYYY-MM-DD, RFC3339, or epoch seconds";
      } else {
        options.history_until = *parsed;
      }
    } else if (arg == "--days") {
      const int days = std::max(1, std::stoi(require_value(arg)));
      options.history_since = Clock::now() - std::chrono::hours(24 * days);
      options.history_until.reset();
    } else if (arg == "--dry-run") {
      options.dry_run = true;
    } else if (arg == "--help" || arg == "-h") {
      command = "help";
    } else if (arg.rfind("--", 0) == 0) {
      error = "unknown option: " + arg;
    }
    if (!error.empty()) {
      return options;
    }
  }

  if (options.provider != "all" && options.provider != "codex" && options.provider != "claude") {
    error = "provider must be all, codex, or claude";
  }
  if (!options.hook_shell.empty() && options.hook_shell != "powershell" && options.hook_shell != "pwsh" &&
      options.hook_shell != "sh") {
    error = "hook shell must be powershell, pwsh, or sh";
  }
  if (options.storage_backend != "auto" && options.storage_backend != "jsonl" &&
      options.storage_backend != "sqlite") {
    error = "storage must be auto, jsonl, or sqlite";
  }
  if (options.history_since && options.history_until && *options.history_since >= *options.history_until) {
    error = "history range must have --since earlier than --until";
  }
  return options;
}

} // namespace catlight
