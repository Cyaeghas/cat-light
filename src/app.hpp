#pragma once

#include "json.hpp"
#include "util.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace catlight {

enum class ProviderId { Codex, Claude };
enum class ProviderState { Ok, MissingAuth, AuthError, NetworkError, ParseError, Stale, Offline };

struct Options {
  std::string provider = "all";
  bool offline = false;
  bool refresh = false;
  bool token_refresh = true;
  int timeout_seconds = 20;
  int max_sessions = 20;
  std::chrono::seconds cache_ttl{60};
  std::string addr = "127.0.0.1:8750";
  std::string hook_shell;
  std::string storage_backend = "auto";
  std::optional<TimePoint> history_since;
  std::optional<TimePoint> history_until;
  bool dry_run = false;
};

struct UsageWindow {
  std::string name;
  int used_percent = 0;
  int remaining_percent = 100;
  std::optional<TimePoint> reset_at;
  std::chrono::seconds window{0};
  int elapsed_percent = 0;
  int pace_delta = 0;
  std::string pace = "on track";
};

struct Credits {
  std::string balance;
  std::string extra_spent;
  std::string extra_limit;
  int extra_percent = 0;
};

struct ProviderStatus {
  ProviderId id = ProviderId::Codex;
  std::string key;
  std::string display_name;
  ProviderState state = ProviderState::Ok;
  std::string message;
  std::string plan;
  std::filesystem::path credential_path;
  std::filesystem::path cache_path;
  bool from_cache = false;
  bool stale = false;
  std::optional<TimePoint> updated_at;
  std::vector<UsageWindow> windows;
  std::optional<Credits> credits;
};

struct Snapshot {
  TimePoint generated_at;
  std::vector<ProviderStatus> statuses;
};

struct DoctorCheck {
  std::string provider;
  std::string display_name;
  std::string credential_label = "auth";
  std::filesystem::path credential_path;
  bool credential_ok = false;
  std::string cache_label = "cache";
  std::filesystem::path cache_path;
  bool cache_ok = false;
  std::string message;
};

Snapshot collect_snapshot(const Options &options);
std::vector<DoctorCheck> run_doctor(const Options &options);

ProviderStatus fetch_codex_status(const Options &options);
ProviderStatus fetch_claude_status(const Options &options);

std::string provider_state_text(ProviderState state);
std::string severity_for_percent(int percent);
std::string overall_severity(const Snapshot &snapshot);
int max_percent(const ProviderStatus &status);
void enrich_window(UsageWindow &window, TimePoint now);

bool provider_selected(const Options &options, const std::string &provider);
Options parse_options(int argc, char **argv, std::string &command, std::string &error);

} // namespace catlight
