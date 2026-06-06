#pragma once

#include "agent.hpp"
#include "app.hpp"

#include <vector>

namespace catlight {

std::vector<AgentEvent> scan_codex_events(const Options &options);
std::vector<AgentEvent> scan_claude_events(const Options &options);
std::vector<AgentEvent> scan_codex_event_history(const Options &options);
std::vector<AgentEvent> scan_claude_event_history(const Options &options);
std::vector<AgentEvent> scan_agent_event_history(const Options &options);
std::vector<AgentSession> collect_agent_sessions(const Options &options);

} // namespace catlight
