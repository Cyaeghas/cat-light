#pragma once

#include "agent.hpp"
#include "app.hpp"

#include <filesystem>
#include <string>
#include <map>
#include <optional>
#include <vector>

namespace catlight {

struct SyncStats {
  std::string backend;
  int scanned = 0;
  int inserted = 0;
  int skipped = 0;
  std::filesystem::path event_path;
  std::filesystem::path sessions_path;
};

struct HistoryAggregate {
  std::string key;
  int events = 0;
  int sessions = 0;
  TokenUsage tokens;
  ContextUsage context;
};

struct HistoryDailyAggregate {
  std::string date;
  int events = 0;
  int sessions = 0;
  TokenUsage tokens;
  ContextUsage context;
};

struct HistorySummary {
  std::string backend;
  std::optional<TimePoint> since;
  std::optional<TimePoint> until;
  int events = 0;
  int sessions = 0;
  TokenUsage tokens;
  ContextUsage context;
  std::vector<HistoryAggregate> providers;
  std::vector<HistoryAggregate> models;
  std::vector<HistoryAggregate> projects;
  std::vector<HistoryAggregate> tools;
  std::vector<HistoryAggregate> commands;
  std::vector<HistoryDailyAggregate> daily;
};

std::filesystem::path history_dir();
std::filesystem::path history_event_log_path();
std::filesystem::path history_sessions_path();
std::filesystem::path history_database_path();

SyncStats sync_history(const Options &options);
std::vector<AgentEvent> read_history_events(const Options &options, std::string *error = nullptr);
std::vector<AgentEvent> read_history_events(std::string *error = nullptr);
std::vector<AgentSession> read_history_sessions(const Options &options);

Json sync_stats_to_json(const SyncStats &stats);
std::string render_sync_text(const SyncStats &stats);
std::string render_history_text(const Options &options);
Json history_to_json(const Options &options);
HistorySummary summarize_history(const Options &options);
Json history_summary_to_json(const HistorySummary &summary);
Json history_trends_to_json(const Options &options);
std::string render_history_summary_text(const HistorySummary &summary);

} // namespace catlight
