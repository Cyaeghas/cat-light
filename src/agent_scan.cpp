#include "agent_scan.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace catlight {
namespace {

std::string string_value(const Json *value, std::string fallback = "") {
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

long long int_value(const Json *value, long long fallback = 0) {
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

std::optional<TimePoint> timestamp_from_json(const Json &obj) {
  for (const std::string key : {"timestamp", "created_at", "time", "ts"}) {
    if (const Json *value = obj.get(key)) {
      if (auto parsed = parse_time_value(*value)) {
        return parsed;
      }
    }
  }
  return std::nullopt;
}

std::filesystem::path codex_sessions_dir() {
  if (auto codex_home = env_var("CODEX_HOME")) {
    return std::filesystem::path(*codex_home) / "sessions";
  }
  return home_dir() / ".codex" / "sessions";
}

std::filesystem::path claude_projects_dir() {
  if (auto config = env_var("CLAUDE_CONFIG_DIR")) {
    return std::filesystem::path(*config) / "projects";
  }
  return home_dir() / ".claude" / "projects";
}

std::vector<std::filesystem::path> list_jsonl_files(const std::filesystem::path &root) {
  std::vector<std::filesystem::path> files;
  std::error_code ec;
  if (!std::filesystem::exists(root, ec)) {
    return files;
  }
  for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
    if (!it->is_regular_file(ec)) {
      continue;
    }
    if (it->path().extension() == ".jsonl") {
      files.push_back(it->path());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

std::string file_session_id(const std::filesystem::path &file) {
  std::string stem = file.stem().string();
  const std::string prefix = "rollout-";
  if (stem.rfind(prefix, 0) == 0) {
    stem = stem.substr(prefix.size());
  }
  return stem.empty() ? file.filename().string() : stem;
}

std::string codex_project_hint(const std::filesystem::path &file) {
  std::vector<std::string> parts;
  auto parent = file.parent_path();
  for (int i = 0; i < 3 && !parent.empty(); ++i) {
    parts.push_back(parent.filename().string());
    parent = parent.parent_path();
  }
  std::reverse(parts.begin(), parts.end());
  return join_strings(parts, "/");
}

std::string claude_project_hint(const std::filesystem::path &file) {
  auto parent = file.parent_path();
  return parent.empty() ? "" : parent.filename().string();
}

bool is_error_output(const std::string &output) {
  if (output.empty()) {
    return false;
  }
  if (output.rfind("Exit code:", 0) == 0) {
    auto line_end = output.find('\n');
    std::string first = line_end == std::string::npos ? output : output.substr(0, line_end);
    auto colon = first.find(':');
    if (colon != std::string::npos) {
      try {
        return std::stoi(trim(first.substr(colon + 1))) != 0;
      } catch (...) {
      }
    }
  }
  const std::string head = output.substr(0, std::min<size_t>(output.size(), 500));
  for (const std::string pattern : {"Traceback", "Error:", "Exception:", "FAILED", "fatal:"}) {
    if (head.find(pattern) != std::string::npos) {
      return true;
    }
  }
  return false;
}

Json parse_arguments(const Json &payload) {
  const std::string args = string_value(payload.get("arguments"));
  if (args.empty()) {
    return Json();
  }
  std::string error;
  Json parsed = Json::parse(args, &error);
  return error.empty() ? parsed : Json();
}

std::string extract_command_hint(const Json &payload) {
  Json args = parse_arguments(payload);
  std::string command = string_value(args.get("command"));
  if (command.empty()) {
    command = string_value(args.get("cmd"));
  }
  command = trim(command);
  if (command.empty()) {
    return "";
  }
  std::istringstream in(command);
  std::string first;
  in >> first;
  if (first.empty() || first[0] == '$' || first.find('\\') != std::string::npos || first.find('/') != std::string::npos) {
    return "command";
  }
  return first;
}

bool requires_approval(const Json &payload) {
  Json args = parse_arguments(payload);
  return string_value(args.get("sandbox_permissions")) == "require_escalated";
}

TokenUsage usage_from_object(const Json *usage_json) {
  TokenUsage usage;
  if (!usage_json || !usage_json->is_object()) {
    return usage;
  }
  usage.input = int_value(usage_json->get("input_tokens"));
  if (usage.input == 0) {
    usage.input = int_value(usage_json->get("input"));
  }
  usage.output = int_value(usage_json->get("output_tokens"));
  if (usage.output == 0) {
    usage.output = int_value(usage_json->get("output"));
  }
  usage.cache_read = int_value(usage_json->get("cached_input_tokens"));
  if (usage.cache_read == 0) {
    usage.cache_read = int_value(usage_json->get("cache_read_input_tokens"));
  }
  usage.cache_write = int_value(usage_json->get("cache_creation_input_tokens"));
  if (usage.cache_write == 0) {
    usage.cache_write = int_value(usage_json->get("cache_write_input_tokens"));
  }
  usage.reasoning = int_value(usage_json->get("reasoning_output_tokens"));
  if (usage.reasoning == 0) {
    usage.reasoning = int_value(usage_json->get("reasoning"));
  }
  usage.total = int_value(usage_json->get("total_tokens"));
  if (usage.total == 0) {
    usage.total = int_value(usage_json->get("total"));
  }
  if (usage.total == 0) {
    usage.total = usage.input + usage.output + usage.cache_read + usage.cache_write + usage.reasoning;
  }
  return usage;
}

ContextUsage context_from_usage(const Json *usage_json, const TokenUsage &usage) {
  ContextUsage context;
  if (!usage_json || !usage_json->is_object()) {
    context.used = usage.total;
    return context;
  }
  context.used = int_value(usage_json->get("context_used"));
  if (context.used == 0) {
    context.used = int_value(usage_json->get("context_tokens"));
  }
  if (context.used == 0) {
    context.used = int_value(usage_json->get("input_tokens")) + int_value(usage_json->get("cached_input_tokens"));
  }
  if (context.used == 0) {
    context.used = usage.total;
  }
  context.limit = int_value(usage_json->get("context_limit"));
  if (context.limit == 0) {
    context.limit = int_value(usage_json->get("context_window"));
  }
  if (context.limit > 0) {
    context.percent = round_percent((static_cast<double>(context.used) / static_cast<double>(context.limit)) * 100.0);
  }
  return context;
}

const Json *codex_token_count_info(const Json &obj) {
  const Json *payload = obj.get("payload");
  if (!payload || !payload->is_object()) {
    return nullptr;
  }
  if (string_value(payload->get("type")) == "token_count") {
    if (const Json *info = payload->get("info"); info && info->is_object()) {
      return info;
    }
  }
  if (const Json *msg = payload->get("msg"); msg && msg->is_object()) {
    if (string_value(msg->get("type")) == "token_count") {
      if (const Json *info = msg->get("info"); info && info->is_object()) {
        return info;
      }
    }
  }
  return nullptr;
}

TokenUsage codex_usage_from_info(const Json *info) {
  if (!info || !info->is_object()) {
    return TokenUsage{};
  }
  if (const Json *total = info->get("total_token_usage"); total && total->is_object()) {
    return usage_from_object(total);
  }
  if (const Json *last = info->get("last_token_usage"); last && last->is_object()) {
    return usage_from_object(last);
  }
  return usage_from_object(info);
}

ContextUsage codex_context_from_info(const Json *info, const TokenUsage &usage) {
  if (!info || !info->is_object()) {
    return context_from_usage(nullptr, usage);
  }
  if (const Json *total = info->get("total_token_usage"); total && total->is_object()) {
    ContextUsage context = context_from_usage(total, usage);
    if (context.limit == 0) {
      context.limit = int_value(info->get("context_window"));
    }
    if (context.limit == 0) {
      context.limit = int_value(info->get("context_limit"));
    }
    if (context.limit > 0) {
      context.percent = round_percent((static_cast<double>(context.used) / static_cast<double>(context.limit)) * 100.0);
    }
    return context;
  }
  return context_from_usage(info, usage);
}

std::optional<AgentEvent> codex_state_event(const Json &obj, const std::string &session_id, const std::string &project_hint) {
  AgentEvent event;
  event.provider = "codex";
  event.source = "codex-log";
  event.session_id = session_id;
  event.instance_id = "codex:" + session_id;
  event.project_path = project_hint;
  event.timestamp = timestamp_from_json(obj).value_or(Clock::now());

  const std::string type = string_value(obj.get("type"));
  const Json *payload = obj.get("payload");
  if (type == "event_msg" && payload && payload->is_object()) {
    const std::string ptype = string_value(payload->get("type"));
    event.raw_kind = ptype;
    if (ptype == "task_complete") {
      event.state = AgentState::Complete;
      event.detail = "Task complete";
      return event;
    }
    if (ptype == "turn_aborted") {
      event.state = AgentState::Error;
      event.detail = "Aborted: " + string_value(payload->get("reason"), "interrupted");
      return event;
    }
    if (ptype == "task_started") {
      event.state = AgentState::Starting;
      event.detail = "Task started";
      return event;
    }
    if (ptype == "user_message") {
      event.state = AgentState::Starting;
      event.detail = "Processing new prompt";
      return event;
    }
    if (ptype == "agent_message") {
      const std::string phase = string_value(payload->get("phase"));
      event.phase = phase;
      if (phase == "final_answer") {
        event.state = AgentState::Complete;
        event.detail = "Final answer";
      } else {
        event.state = AgentState::Working;
        event.detail = "Generating";
      }
      return event;
    }
  }

  if (type == "response_item" && payload && payload->is_object()) {
    const std::string ptype = string_value(payload->get("type"));
    event.raw_kind = ptype;
    if (ptype == "reasoning") {
      event.state = AgentState::Thinking;
      event.detail = "Thinking";
      return event;
    }
    if (ptype == "function_call") {
      const std::string command = extract_command_hint(*payload);
      event.tool_name = string_value(payload->get("name"), "shell_command");
      event.state = requires_approval(*payload) ? AgentState::Waiting : AgentState::Working;
      event.detail = event.state == AgentState::Waiting ? "Waiting for approval" : "Running command";
      if (!command.empty()) {
        event.detail += " (" + command + ")";
      }
      return event;
    }
    if (ptype == "function_call_output") {
      event.tool_name = "shell_command";
      const std::string output = string_value(payload->get("output"));
      event.state = is_error_output(output) ? AgentState::Error : AgentState::Working;
      event.detail = event.state == AgentState::Error ? "Command failed" : "Command completed";
      return event;
    }
    if (ptype == "custom_tool_call") {
      event.tool_name = string_value(payload->get("name"), "apply_patch");
      event.state = AgentState::Working;
      event.detail = "Editing file";
      return event;
    }
    if (ptype == "custom_tool_call_output") {
      event.tool_name = "apply_patch";
      const std::string output = string_value(payload->get("output"));
      event.state = is_error_output(output) ? AgentState::Error : AgentState::Working;
      event.detail = event.state == AgentState::Error ? "Patch failed" : "Patch applied";
      return event;
    }
    if (ptype == "message") {
      const std::string role = string_value(payload->get("role"));
      const std::string phase = string_value(payload->get("phase"));
      event.phase = phase;
      if (role == "assistant" && phase == "final_answer") {
        event.state = AgentState::Complete;
        event.detail = "Final answer";
        return event;
      }
      if (role == "assistant" && phase == "commentary") {
        event.state = AgentState::Working;
        event.detail = "Generating";
        return event;
      }
    }
  }

  return std::nullopt;
}

bool content_has_type(const Json *content, const std::string &type) {
  if (!content || !content->is_array()) {
    return false;
  }
  for (const auto &item : content->array()) {
    if (item.is_object() && string_value(item.get("type")) == type) {
      return true;
    }
  }
  return false;
}

std::string last_tool_name(const Json *content) {
  std::string name;
  if (!content || !content->is_array()) {
    return name;
  }
  for (const auto &item : content->array()) {
    if (!item.is_object()) {
      continue;
    }
    const std::string type = string_value(item.get("type"));
    if (type == "tool_use" || type == "server_tool_use") {
      name = string_value(item.get("name"), "tool");
    }
  }
  return name;
}

std::optional<AgentEvent> claude_state_event(const Json &obj, const std::string &session_id, const std::string &project_hint) {
  AgentEvent event;
  event.provider = "claude";
  event.source = "claude-log";
  event.session_id = session_id;
  event.instance_id = "claude:" + session_id;
  event.project_path = project_hint;
  event.timestamp = timestamp_from_json(obj).value_or(Clock::now());

  const std::string type = string_value(obj.get("type"));
  event.raw_kind = type;
  if (type == "assistant") {
    const Json *message = obj.get("message");
    if (!message || !message->is_object()) {
      return std::nullopt;
    }
    const std::string stop_reason = string_value(message->get("stop_reason"));
    const Json *content = message->get("content");
    const bool thinking = content_has_type(content, "thinking");
    const std::string tool = last_tool_name(content);
    event.tool_name = tool;
    if (thinking) {
      event.state = AgentState::Thinking;
      event.detail = tool.empty() ? "Thinking" : "Thinking before " + tool;
      return event;
    }
    if (stop_reason == "tool_use") {
      event.state = AgentState::Waiting;
      event.detail = tool.empty() ? "Waiting for tool" : "Waiting: " + tool;
      return event;
    }
    if (stop_reason == "end_turn") {
      event.state = AgentState::Complete;
      event.detail = tool.empty() ? "Response complete" : tool + " complete";
      return event;
    }
    if (stop_reason.empty()) {
      event.state = AgentState::Working;
      event.detail = "Generating response";
      return event;
    }
  }
  if (type == "user") {
    const Json *message = obj.get("message");
    const Json *content = message && message->is_object() ? message->get("content") : nullptr;
    if (content_has_type(content, "tool_result")) {
      event.state = AgentState::Working;
      event.detail = "Tool result received";
      return event;
    }
    if (obj.get("promptId")) {
      event.state = AgentState::Starting;
      event.detail = "Processing new prompt";
      return event;
    }
  }
  if (type == "system") {
    const std::string subtype = string_value(obj.get("subtype"));
    if (subtype == "stop_hook_summary") {
      event.state = AgentState::Complete;
      event.detail = "Task complete";
      return event;
    }
  }
  if (type == "attachment") {
    const Json *attachment = obj.get("attachment");
    if (!attachment || !attachment->is_object()) {
      return std::nullopt;
    }
    const std::string attachment_type = string_value(attachment->get("type"));
    const std::string hook_event = string_value(attachment->get("hookEvent"));
    const std::string hook_name = string_value(attachment->get("hookName"));
    event.raw_kind = hook_event.empty() ? attachment_type : hook_event;
    event.tool_name = hook_name;
    if (attachment_type == "hook_non_blocking_error") {
      event.state = AgentState::Error;
      event.detail = hook_name.empty() ? "Hook error" : "Hook error: " + hook_name;
      return event;
    }
    if (hook_event == "Stop") {
      event.state = AgentState::Complete;
      event.detail = "Task complete";
      return event;
    }
    if (hook_event == "UserPromptSubmit") {
      event.state = AgentState::Starting;
      event.detail = "Processing new prompt";
      return event;
    }
    if (hook_event == "PreToolUse") {
      event.state = AgentState::Working;
      event.detail = hook_name.empty() ? "Preparing tool" : "Preparing " + hook_name;
      return event;
    }
  }
  return std::nullopt;
}

TokenUsage claude_usage_from_obj(const Json &obj) {
  if (const Json *usage = obj.path({"message", "usage"}); usage && usage->is_object()) {
    return usage_from_object(usage);
  }
  if (const Json *usage = obj.get("usage"); usage && usage->is_object()) {
    return usage_from_object(usage);
  }
  return TokenUsage{};
}

std::string claude_model_from_obj(const Json &obj) {
  std::string model = string_value(obj.path({"message", "model"}));
  if (model.empty()) {
    model = string_value(obj.get("model"));
  }
  return model;
}

std::vector<AgentEvent> parse_file_events(const std::filesystem::path &file,
                                          const std::string &provider,
                                          const std::string &session_id,
                                          const std::string &project_hint) {
  std::vector<AgentEvent> events;
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    return events;
  }

  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty()) {
      continue;
    }
    std::string parse_error;
    Json obj = Json::parse(line, &parse_error);
    if (!parse_error.empty() || !obj.is_object()) {
      continue;
    }
    if (provider == "codex") {
      if (const Json *info = codex_token_count_info(obj)) {
        AgentEvent usage;
        usage.provider = "codex";
        usage.source = "codex-log";
        usage.session_id = session_id;
        usage.instance_id = "codex:" + session_id;
        usage.project_path = project_hint;
        usage.state = AgentState::Working;
        usage.phase = "token_count";
        usage.detail = "Token count";
        usage.raw_kind = "token_count";
        usage.timestamp = timestamp_from_json(obj).value_or(Clock::now());
        usage.tokens = codex_usage_from_info(info);
        usage.context = codex_context_from_info(info, usage.tokens);
        events.push_back(usage);
      }
      if (auto state = codex_state_event(obj, session_id, project_hint)) {
        events.push_back(*state);
      }
    } else {
      TokenUsage usage_tokens = claude_usage_from_obj(obj);
      if (usage_tokens.total > 0) {
        AgentEvent usage;
        usage.provider = "claude";
        usage.source = "claude-log";
        usage.session_id = session_id;
        usage.instance_id = "claude:" + session_id;
        usage.project_path = project_hint;
        usage.state = AgentState::Working;
        usage.phase = "usage";
        usage.detail = "Token usage";
        usage.raw_kind = "usage";
        usage.timestamp = timestamp_from_json(obj).value_or(Clock::now());
        usage.model = claude_model_from_obj(obj);
        usage.tokens = usage_tokens;
        usage.context = context_from_usage(obj.path({"message", "usage"}), usage.tokens);
        events.push_back(usage);
      }
      if (auto state = claude_state_event(obj, session_id, project_hint)) {
        std::string model = claude_model_from_obj(obj);
        if (!model.empty()) {
          state->model = model;
        }
        events.push_back(*state);
      }
    }
  }
  return events;
}

bool is_usage_event(const AgentEvent &event) {
  return event.raw_kind == "token_count" || event.raw_kind == "usage" || event.phase == "usage" ||
         event.phase == "token_count";
}

std::optional<AgentEvent> parse_file_latest(const std::filesystem::path &file,
                                            const std::string &provider,
                                            const std::string &session_id,
                                            const std::string &project_hint) {
  std::optional<AgentEvent> latest_state;
  std::optional<AgentEvent> latest_usage;
  for (const auto &event : parse_file_events(file, provider, session_id, project_hint)) {
    if (is_usage_event(event)) {
      if (!latest_usage || event.timestamp >= latest_usage->timestamp) {
        latest_usage = event;
      }
    } else if (!latest_state || event.timestamp >= latest_state->timestamp) {
      latest_state = event;
    }
  }

  if (latest_usage && latest_state) {
    latest_state->tokens = latest_usage->tokens;
    latest_state->context = latest_usage->context;
    if (latest_state->model.empty()) {
      latest_state->model = latest_usage->model;
    }
    return latest_state;
  }
  if (latest_state) {
    return latest_state;
  }
  return latest_usage;
}

void mark_stale_if_needed(AgentEvent &event) {
  const auto age = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - event.timestamp);
  if (age > std::chrono::minutes(5) &&
      (event.state == AgentState::Starting || event.state == AgentState::Thinking || event.state == AgentState::Working)) {
    event.state = AgentState::Stale;
    if (event.detail.empty()) {
      event.detail = "Last active " + human_age(event.timestamp, Clock::now()) + " ago";
    }
  }
}

} // namespace

std::vector<AgentEvent> scan_codex_events(const Options &options) {
  std::vector<AgentEvent> events;
  if (!provider_selected(options, "codex")) {
    return events;
  }
  for (const auto &file : list_jsonl_files(codex_sessions_dir())) {
    auto event = parse_file_latest(file, "codex", file_session_id(file), codex_project_hint(file));
    if (event) {
      mark_stale_if_needed(*event);
      events.push_back(*event);
    }
  }
  return events;
}

std::vector<AgentEvent> scan_codex_event_history(const Options &options) {
  std::vector<AgentEvent> events;
  if (!provider_selected(options, "codex")) {
    return events;
  }
  for (const auto &file : list_jsonl_files(codex_sessions_dir())) {
    auto file_events = parse_file_events(file, "codex", file_session_id(file), codex_project_hint(file));
    events.insert(events.end(), file_events.begin(), file_events.end());
  }
  return events;
}

std::vector<AgentEvent> scan_claude_events(const Options &options) {
  std::vector<AgentEvent> events;
  if (!provider_selected(options, "claude")) {
    return events;
  }
  for (const auto &file : list_jsonl_files(claude_projects_dir())) {
    auto event = parse_file_latest(file, "claude", file_session_id(file), claude_project_hint(file));
    if (event) {
      mark_stale_if_needed(*event);
      events.push_back(*event);
    }
  }
  return events;
}

std::vector<AgentEvent> scan_claude_event_history(const Options &options) {
  std::vector<AgentEvent> events;
  if (!provider_selected(options, "claude")) {
    return events;
  }
  for (const auto &file : list_jsonl_files(claude_projects_dir())) {
    auto file_events = parse_file_events(file, "claude", file_session_id(file), claude_project_hint(file));
    events.insert(events.end(), file_events.begin(), file_events.end());
  }
  return events;
}

std::vector<AgentEvent> scan_agent_event_history(const Options &options) {
  std::vector<AgentEvent> events;
  auto codex = scan_codex_event_history(options);
  events.insert(events.end(), codex.begin(), codex.end());
  auto claude = scan_claude_event_history(options);
  events.insert(events.end(), claude.begin(), claude.end());
  return events;
}

std::vector<AgentSession> collect_agent_sessions(const Options &options) {
  std::vector<AgentEvent> events;
  std::string read_error;
  auto manual = read_agent_events(&read_error);
  events.insert(events.end(), manual.begin(), manual.end());
  auto codex = scan_codex_events(options);
  events.insert(events.end(), codex.begin(), codex.end());
  auto claude = scan_claude_events(options);
  events.insert(events.end(), claude.begin(), claude.end());
  auto sessions = merge_agent_events(events);
  if (static_cast<int>(sessions.size()) > options.max_sessions) {
    sessions.resize(static_cast<size_t>(options.max_sessions));
  }
  return sessions;
}

} // namespace catlight
