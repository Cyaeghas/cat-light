#pragma once

#include "agent.hpp"
#include "app.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace catlight {

struct AgentLogHealth {
  std::string provider;
  std::filesystem::path root;
  int files = 0;
  int events = 0;
  int parse_errors = 0;
  std::optional<TimePoint> latest_activity;
  std::filesystem::path latest_file;
};

std::vector<AgentEvent> scan_codex_events(const Options &options);
std::vector<AgentEvent> scan_claude_events(const Options &options);
std::vector<AgentEvent> scan_codex_event_history(const Options &options);
std::vector<AgentEvent> scan_claude_event_history(const Options &options);
std::vector<AgentEvent> scan_agent_event_history(const Options &options);
std::vector<AgentSession> collect_agent_sessions(const Options &options);
AgentLogHealth scan_codex_log_health();
AgentLogHealth scan_claude_log_health();

} // namespace catlight
