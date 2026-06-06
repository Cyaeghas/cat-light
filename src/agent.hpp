#pragma once

#include "json.hpp"
#include "util.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace catlight {

enum class AgentState {
  Starting,
  Thinking,
  Working,
  Waiting,
  Complete,
  Error,
  Idle,
  Stale,
};

struct TokenUsage {
  long long input = 0;
  long long output = 0;
  long long cache_read = 0;
  long long cache_write = 0;
  long long reasoning = 0;
  long long total = 0;
};

struct ContextUsage {
  long long used = 0;
  long long limit = 0;
  int percent = 0;
};

struct AgentEvent {
  std::string schema = "cat-light.event.v1";
  std::string provider;
  std::string source = "manual";
  std::string session_id;
  std::string instance_id;
  std::string project_path;
  int pid = 0;
  std::string model;
  AgentState state = AgentState::Working;
  std::string phase;
  std::string detail;
  std::string tool_name;
  TimePoint timestamp = Clock::now();
  TokenUsage tokens;
  ContextUsage context;
  std::string raw_kind;
};

struct AgentSession {
  std::string provider;
  std::string session_id;
  std::string instance_id;
  std::string project_path;
  int pid = 0;
  std::string model;
  AgentState state = AgentState::Idle;
  std::string phase;
  std::string detail;
  std::string tool_name;
  TimePoint last_activity = Clock::now();
  TokenUsage tokens;
  ContextUsage context;
  std::vector<std::string> sources;
};

std::string agent_state_text(AgentState state);
AgentState parse_agent_state(const std::string &state);
int agent_state_priority(AgentState state);

Json token_usage_to_json(const TokenUsage &usage);
Json context_usage_to_json(const ContextUsage &usage);
Json agent_event_to_json(const AgentEvent &event);
Json agent_session_to_json(const AgentSession &session);

std::optional<AgentEvent> agent_event_from_json(const Json &json, std::string *error = nullptr);
std::optional<AgentEvent> agent_event_from_cli(int argc, char **argv, int first_arg, std::string *error = nullptr);

std::filesystem::path data_root();
std::filesystem::path event_log_path();
bool append_agent_event(const AgentEvent &event, std::string *error = nullptr);
std::vector<AgentEvent> read_agent_events(std::string *error = nullptr);

std::vector<AgentSession> merge_agent_events(const std::vector<AgentEvent> &events);
Json agent_sessions_to_json(const std::vector<AgentSession> &sessions);
std::string render_agent_sessions_text(const std::vector<AgentSession> &sessions);
std::string render_agent_state_text(const std::vector<AgentSession> &sessions);
std::string render_agent_waybar_json(const std::vector<AgentSession> &sessions);

} // namespace catlight
