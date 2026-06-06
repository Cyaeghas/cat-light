#include "agent.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace catlight {
namespace {

long long json_i64(const Json *value, long long fallback = 0) {
  if (!value) {
    return fallback;
  }
  if (value->is_number()) {
    return static_cast<long long>(value->as_number());
  }
  if (value->is_string()) {
    try {
      return std::stoll(value->as_string());
    } catch (...) {
    }
  }
  return fallback;
}

int json_int(const Json *value, int fallback = 0) {
  return static_cast<int>(json_i64(value, fallback));
}

std::string json_string(const Json *value, std::string fallback = "") {
  if (!value) {
    return fallback;
  }
  if (value->is_string()) {
    return value->as_string();
  }
  if (value->is_number()) {
    std::ostringstream out;
    out << static_cast<long long>(value->as_number());
    return out.str();
  }
  return fallback;
}

TokenUsage token_usage_from_json(const Json *json) {
  TokenUsage usage;
  if (!json || !json->is_object()) {
    return usage;
  }
  usage.input = json_i64(json->get("input"));
  if (usage.input == 0) {
    usage.input = json_i64(json->get("input_tokens"));
  }
  usage.output = json_i64(json->get("output"));
  if (usage.output == 0) {
    usage.output = json_i64(json->get("output_tokens"));
  }
  usage.cache_read = json_i64(json->get("cache_read"));
  if (usage.cache_read == 0) {
    usage.cache_read = json_i64(json->get("cached_input_tokens"));
  }
  usage.cache_write = json_i64(json->get("cache_write"));
  if (usage.cache_write == 0) {
    usage.cache_write = json_i64(json->get("cache_creation_input_tokens"));
  }
  usage.reasoning = json_i64(json->get("reasoning"));
  if (usage.reasoning == 0) {
    usage.reasoning = json_i64(json->get("reasoning_output_tokens"));
  }
  usage.total = json_i64(json->get("total"));
  if (usage.total == 0) {
    usage.total = json_i64(json->get("total_tokens"));
  }
  if (usage.total == 0) {
    usage.total = usage.input + usage.output + usage.cache_read + usage.cache_write + usage.reasoning;
  }
  return usage;
}

ContextUsage context_usage_from_json(const Json *json) {
  ContextUsage context;
  if (!json || !json->is_object()) {
    return context;
  }
  context.used = json_i64(json->get("used"));
  if (context.used == 0) {
    context.used = json_i64(json->get("context_used"));
  }
  context.limit = json_i64(json->get("limit"));
  if (context.limit == 0) {
    context.limit = json_i64(json->get("context_limit"));
  }
  context.percent = json_int(json->get("percent"));
  if (context.percent == 0 && context.limit > 0) {
    context.percent = round_percent((static_cast<double>(context.used) / static_cast<double>(context.limit)) * 100.0);
  }
  context.percent = clamp_percent(context.percent);
  return context;
}

void keep_max(TokenUsage &target, const TokenUsage &add) {
  target.input = std::max(target.input, add.input);
  target.output = std::max(target.output, add.output);
  target.cache_read = std::max(target.cache_read, add.cache_read);
  target.cache_write = std::max(target.cache_write, add.cache_write);
  target.reasoning = std::max(target.reasoning, add.reasoning);
  target.total = std::max(target.total, add.total);
  if (target.total == 0) {
    target.total = target.input + target.output + target.cache_read + target.cache_write + target.reasoning;
  }
}

void merge_context(ContextUsage &target, const ContextUsage &add) {
  if (add.used > 0 || add.limit > 0 || add.percent > 0) {
    target = add;
  }
}

std::string default_session_id(const AgentEvent &event) {
  if (!event.session_id.empty()) {
    return event.session_id;
  }
  std::vector<std::string> parts;
  parts.push_back(event.provider.empty() ? "unknown" : event.provider);
  if (!event.project_path.empty()) {
    parts.push_back(event.project_path);
  }
  if (event.pid > 0) {
    parts.push_back(std::to_string(event.pid));
  }
  if (parts.size() == 1) {
    parts.push_back("default");
  }
  return join_strings(parts, ":");
}

std::string provider_label(const std::string &provider) {
  if (provider == "codex") {
    return "Codex";
  }
  if (provider == "claude") {
    return "Claude";
  }
  if (provider.empty()) {
    return "Agent";
  }
  std::string out = provider;
  out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
  return out;
}

std::string short_session_id(const std::string &id) {
  if (id.size() <= 10) {
    return id;
  }
  return id.substr(0, 8);
}

std::string format_tokens(long long value) {
  if (value < 1000) {
    return std::to_string(value);
  }
  std::ostringstream out;
  if (value < 1000000) {
    out << (value / 1000) << "k";
  } else {
    out << (value / 1000000) << "m";
  }
  return out.str();
}

bool is_usage_only_event(const AgentEvent &event) {
  return event.raw_kind == "token_count" || event.raw_kind == "usage" || event.phase == "usage" ||
         event.phase == "token_count";
}

} // namespace

std::string agent_state_text(AgentState state) {
  switch (state) {
  case AgentState::Starting:
    return "starting";
  case AgentState::Thinking:
    return "thinking";
  case AgentState::Working:
    return "working";
  case AgentState::Waiting:
    return "waiting";
  case AgentState::Complete:
    return "complete";
  case AgentState::Error:
    return "error";
  case AgentState::Idle:
    return "idle";
  case AgentState::Stale:
    return "stale";
  }
  return "idle";
}

AgentState parse_agent_state(const std::string &state) {
  const std::string s = to_lower(state);
  if (s == "start" || s == "started" || s == "starting") {
    return AgentState::Starting;
  }
  if (s == "think" || s == "thinking") {
    return AgentState::Thinking;
  }
  if (s == "work" || s == "working" || s == "running") {
    return AgentState::Working;
  }
  if (s == "wait" || s == "waiting" || s == "blocked") {
    return AgentState::Waiting;
  }
  if (s == "done" || s == "complete" || s == "completed") {
    return AgentState::Complete;
  }
  if (s == "error" || s == "failed" || s == "aborted") {
    return AgentState::Error;
  }
  if (s == "stale") {
    return AgentState::Stale;
  }
  return AgentState::Idle;
}

int agent_state_priority(AgentState state) {
  switch (state) {
  case AgentState::Error:
    return 80;
  case AgentState::Waiting:
    return 70;
  case AgentState::Working:
    return 60;
  case AgentState::Thinking:
    return 50;
  case AgentState::Starting:
    return 40;
  case AgentState::Complete:
    return 30;
  case AgentState::Idle:
    return 20;
  case AgentState::Stale:
    return 10;
  }
  return 0;
}

Json token_usage_to_json(const TokenUsage &usage) {
  Json::object_type obj;
  obj["input"] = static_cast<double>(usage.input);
  obj["output"] = static_cast<double>(usage.output);
  obj["cache_read"] = static_cast<double>(usage.cache_read);
  obj["cache_write"] = static_cast<double>(usage.cache_write);
  obj["reasoning"] = static_cast<double>(usage.reasoning);
  obj["total"] = static_cast<double>(usage.total);
  return Json(std::move(obj));
}

Json context_usage_to_json(const ContextUsage &usage) {
  Json::object_type obj;
  obj["used"] = static_cast<double>(usage.used);
  obj["limit"] = static_cast<double>(usage.limit);
  obj["percent"] = usage.percent;
  return Json(std::move(obj));
}

Json agent_event_to_json(const AgentEvent &event) {
  Json::object_type obj;
  obj["schema"] = event.schema;
  obj["provider"] = event.provider;
  obj["source"] = event.source;
  obj["session_id"] = event.session_id;
  obj["instance_id"] = event.instance_id;
  obj["project_path"] = event.project_path;
  obj["pid"] = event.pid;
  obj["model"] = event.model;
  obj["state"] = agent_state_text(event.state);
  obj["phase"] = event.phase;
  obj["detail"] = event.detail;
  obj["tool_name"] = event.tool_name;
  obj["timestamp"] = format_iso_utc(event.timestamp);
  obj["tokens"] = token_usage_to_json(event.tokens);
  obj["context"] = context_usage_to_json(event.context);
  obj["raw_kind"] = event.raw_kind;
  return Json(std::move(obj));
}

Json agent_session_to_json(const AgentSession &session) {
  Json::object_type obj;
  obj["provider"] = session.provider;
  obj["session_id"] = session.session_id;
  obj["instance_id"] = session.instance_id;
  obj["project_path"] = session.project_path;
  obj["pid"] = session.pid;
  obj["model"] = session.model;
  obj["state"] = agent_state_text(session.state);
  obj["phase"] = session.phase;
  obj["detail"] = session.detail;
  obj["tool_name"] = session.tool_name;
  obj["last_activity"] = format_iso_utc(session.last_activity);
  obj["age"] = human_age(session.last_activity, Clock::now());
  obj["tokens"] = token_usage_to_json(session.tokens);
  obj["context"] = context_usage_to_json(session.context);
  Json::array_type sources;
  for (const auto &source : session.sources) {
    sources.push_back(source);
  }
  obj["sources"] = Json(std::move(sources));
  return Json(std::move(obj));
}

std::optional<AgentEvent> agent_event_from_json(const Json &json, std::string *error) {
  if (!json.is_object()) {
    if (error) {
      *error = "event JSON must be an object";
    }
    return std::nullopt;
  }
  AgentEvent event;
  event.schema = json_string(json.get("schema"), "cat-light.event.v1");
  event.provider = to_lower(json_string(json.get("provider")));
  event.source = json_string(json.get("source"), "manual");
  event.session_id = json_string(json.get("session_id"));
  event.instance_id = json_string(json.get("instance_id"));
  event.project_path = json_string(json.get("project_path"));
  event.pid = json_int(json.get("pid"));
  event.model = json_string(json.get("model"));
  event.state = parse_agent_state(json_string(json.get("state"), "working"));
  event.phase = json_string(json.get("phase"));
  event.detail = json_string(json.get("detail"));
  event.tool_name = json_string(json.get("tool_name"));
  event.raw_kind = json_string(json.get("raw_kind"));
  event.tokens = token_usage_from_json(json.get("tokens"));
  event.context = context_usage_from_json(json.get("context"));
  if (const Json *ts = json.get("timestamp")) {
    if (auto parsed = parse_time_value(*ts)) {
      event.timestamp = *parsed;
    }
  }
  if (event.provider.empty()) {
    if (error) {
      *error = "event provider is required";
    }
    return std::nullopt;
  }
  if (event.session_id.empty()) {
    event.session_id = default_session_id(event);
  }
  if (event.instance_id.empty()) {
    event.instance_id = event.provider + ":" + event.session_id;
  }
  return event;
}

std::optional<AgentEvent> agent_event_from_cli(int argc, char **argv, int first_arg, std::string *error) {
  AgentEvent event;
  event.source = "cli";
  for (int i = first_arg; i < argc; ++i) {
    std::string arg = argv[i];
    auto require_value = [&](const std::string &name) -> std::string {
      if (i + 1 >= argc) {
        if (error) {
          *error = "missing value for " + name;
        }
        return "";
      }
      return argv[++i];
    };
    if (arg == "--provider") {
      event.provider = to_lower(require_value(arg));
    } else if (arg == "--source") {
      event.source = require_value(arg);
    } else if (arg == "--session-id") {
      event.session_id = require_value(arg);
    } else if (arg == "--instance-id") {
      event.instance_id = require_value(arg);
    } else if (arg == "--project-path") {
      event.project_path = require_value(arg);
    } else if (arg == "--pid") {
      event.pid = std::stoi(require_value(arg));
    } else if (arg == "--model") {
      event.model = require_value(arg);
    } else if (arg == "--state") {
      event.state = parse_agent_state(require_value(arg));
    } else if (arg == "--phase") {
      event.phase = require_value(arg);
    } else if (arg == "--detail") {
      event.detail = require_value(arg);
    } else if (arg == "--tool-name") {
      event.tool_name = require_value(arg);
    } else if (arg == "--raw-kind") {
      event.raw_kind = require_value(arg);
    } else if (arg == "--input-tokens") {
      event.tokens.input = std::stoll(require_value(arg));
    } else if (arg == "--output-tokens") {
      event.tokens.output = std::stoll(require_value(arg));
    } else if (arg == "--cache-read-tokens") {
      event.tokens.cache_read = std::stoll(require_value(arg));
    } else if (arg == "--cache-write-tokens") {
      event.tokens.cache_write = std::stoll(require_value(arg));
    } else if (arg == "--reasoning-tokens") {
      event.tokens.reasoning = std::stoll(require_value(arg));
    } else if (arg == "--total-tokens") {
      event.tokens.total = std::stoll(require_value(arg));
    } else if (arg == "--context-used") {
      event.context.used = std::stoll(require_value(arg));
    } else if (arg == "--context-limit") {
      event.context.limit = std::stoll(require_value(arg));
    } else if (arg == "--context-percent") {
      event.context.percent = clamp_percent(std::stoi(require_value(arg)));
    } else if (arg == "--stdin") {
      continue;
    } else if (arg.rfind("--", 0) == 0) {
      if (error) {
        *error = "unknown event option: " + arg;
      }
      return std::nullopt;
    }
    if (error && !error->empty()) {
      return std::nullopt;
    }
  }
  if (event.provider.empty()) {
    if (error) {
      *error = "event --provider is required";
    }
    return std::nullopt;
  }
  if (event.session_id.empty()) {
    event.session_id = default_session_id(event);
  }
  if (event.instance_id.empty()) {
    event.instance_id = event.provider + ":" + event.session_id;
  }
  if (event.tokens.total == 0) {
    event.tokens.total = event.tokens.input + event.tokens.output + event.tokens.cache_read +
                         event.tokens.cache_write + event.tokens.reasoning;
  }
  if (event.context.percent == 0 && event.context.limit > 0) {
    event.context.percent = round_percent((static_cast<double>(event.context.used) /
                                           static_cast<double>(event.context.limit)) *
                                          100.0);
  }
  return event;
}

std::filesystem::path data_root() {
  if (auto dir = env_var("CAT_LIGHT_DATA_DIR")) {
    return *dir;
  }
#ifdef _WIN32
  if (auto local = env_var("LOCALAPPDATA")) {
    return std::filesystem::path(*local) / "cat-light";
  }
#elif defined(__APPLE__)
  return home_dir() / "Library" / "Application Support" / "cat-light";
#else
  if (auto xdg = env_var("XDG_DATA_HOME")) {
    return std::filesystem::path(*xdg) / "cat-light";
  }
#endif
  return home_dir() / ".local" / "share" / "cat-light";
}

std::filesystem::path event_log_path() {
  return data_root() / "events.jsonl";
}

bool append_agent_event(const AgentEvent &event, std::string *error) {
  try {
    auto path = event_log_path();
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out) {
      if (error) {
        *error = "cannot open event log";
      }
      return false;
    }
    out << agent_event_to_json(event).dump() << "\n";
    return true;
  } catch (const std::exception &e) {
    if (error) {
      *error = e.what();
    }
    return false;
  }
}

std::vector<AgentEvent> read_agent_events(std::string *error) {
  std::vector<AgentEvent> events;
  std::ifstream in(event_log_path(), std::ios::binary);
  if (!in) {
    return events;
  }
  std::string line;
  int line_no = 0;
  while (std::getline(in, line)) {
    ++line_no;
    line = trim(line);
    if (line.empty()) {
      continue;
    }
    std::string parse_error;
    Json json = Json::parse(line, &parse_error);
    if (!parse_error.empty()) {
      if (error) {
        *error = "event log parse failed at line " + std::to_string(line_no) + ": " + parse_error;
      }
      continue;
    }
    std::string event_error;
    if (auto event = agent_event_from_json(json, &event_error)) {
      events.push_back(*event);
    }
  }
  return events;
}

std::vector<AgentSession> merge_agent_events(const std::vector<AgentEvent> &events) {
  std::map<std::string, AgentSession> sessions;
  std::map<std::string, std::set<std::string>> sources;
  for (const auto &event : events) {
    const std::string session_id = default_session_id(event);
    const std::string instance_id = event.instance_id.empty() ? event.provider + ":" + session_id : event.instance_id;
    AgentSession &session = sessions[instance_id];
    if (session.instance_id.empty()) {
      session.provider = event.provider;
      session.session_id = session_id;
      session.instance_id = instance_id;
      session.last_activity = event.timestamp;
      session.state = event.state;
    }
    if (!event.project_path.empty()) {
      session.project_path = event.project_path;
    }
    if (event.pid > 0) {
      session.pid = event.pid;
    }
    if (!event.model.empty()) {
      session.model = event.model;
    }
    keep_max(session.tokens, event.tokens);
    merge_context(session.context, event.context);
    sources[instance_id].insert(event.source);
    const bool usage_only = is_usage_only_event(event);
    if (event.timestamp > session.last_activity) {
      session.last_activity = event.timestamp;
      if (!usage_only) {
        session.state = event.state;
        session.phase = event.phase;
        session.detail = event.detail;
        session.tool_name = event.tool_name;
      }
    } else if (event.timestamp == session.last_activity && !usage_only) {
      session.state = event.state;
      session.phase = event.phase;
      session.detail = event.detail;
      session.tool_name = event.tool_name;
    }
  }
  std::vector<AgentSession> out;
  for (auto &entry : sessions) {
    auto &session = entry.second;
    for (const auto &source : sources[entry.first]) {
      session.sources.push_back(source);
    }
    out.push_back(session);
  }
  std::sort(out.begin(), out.end(), [](const AgentSession &a, const AgentSession &b) {
    if (agent_state_priority(a.state) != agent_state_priority(b.state)) {
      return agent_state_priority(a.state) > agent_state_priority(b.state);
    }
    return a.last_activity > b.last_activity;
  });
  return out;
}

Json agent_sessions_to_json(const std::vector<AgentSession> &sessions) {
  Json::object_type root;
  root["generated_at"] = format_iso_utc(Clock::now());
  Json::array_type items;
  for (const auto &session : sessions) {
    items.push_back(agent_session_to_json(session));
  }
  root["sessions"] = Json(std::move(items));
  return Json(std::move(root));
}

std::string render_agent_sessions_text(const std::vector<AgentSession> &sessions) {
  if (sessions.empty()) {
    return "No agent sessions found.";
  }
  std::ostringstream out;
  for (size_t i = 0; i < sessions.size(); ++i) {
    const auto &s = sessions[i];
    if (i) {
      out << "\n";
    }
    out << provider_label(s.provider) << " " << short_session_id(s.session_id)
        << " [" << agent_state_text(s.state) << "]";
    if (!s.model.empty()) {
      out << " " << s.model;
    }
    if (!s.project_path.empty()) {
      out << "\n  project: " << s.project_path;
    }
    if (!s.detail.empty()) {
      out << "\n  detail: " << s.detail;
    }
    out << "\n  last: " << human_age(s.last_activity, Clock::now()) << " ago";
    if (s.tokens.total > 0) {
      out << ", tokens: " << format_tokens(s.tokens.total);
    }
    if (s.context.used > 0 || s.context.limit > 0) {
      out << ", context: " << s.context.percent << "%";
      if (s.context.limit > 0) {
        out << " (" << s.context.used << "/" << s.context.limit << ")";
      }
    }
  }
  return out.str();
}

std::string render_agent_state_text(const std::vector<AgentSession> &sessions) {
  if (sessions.empty()) {
    return "Agents idle";
  }
  std::map<std::string, std::map<std::string, int>> grouped;
  for (const auto &session : sessions) {
    grouped[session.provider][agent_state_text(session.state)]++;
  }
  std::vector<std::string> parts;
  for (const auto &provider : grouped) {
    std::vector<std::string> states;
    for (const auto &state : provider.second) {
      states.push_back(std::to_string(state.second) + " " + state.first);
    }
    parts.push_back(provider_label(provider.first) + " " + join_strings(states, ", "));
  }
  return join_strings(parts, " | ");
}

std::string render_agent_waybar_json(const std::vector<AgentSession> &sessions) {
  AgentState top = AgentState::Idle;
  for (const auto &session : sessions) {
    if (agent_state_priority(session.state) > agent_state_priority(top)) {
      top = session.state;
    }
  }
  std::string klass = agent_state_text(top);
  if (top == AgentState::Complete || top == AgentState::Idle || top == AgentState::Stale) {
    klass = "low";
  } else if (top == AgentState::Thinking || top == AgentState::Starting || top == AgentState::Working) {
    klass = "mid";
  } else if (top == AgentState::Waiting) {
    klass = "high";
  } else if (top == AgentState::Error) {
    klass = "critical";
  }
  Json::object_type obj;
  obj["text"] = render_agent_state_text(sessions);
  obj["tooltip"] = render_agent_sessions_text(sessions);
  obj["class"] = klass;
  return Json(std::move(obj)).dump();
}

} // namespace catlight
