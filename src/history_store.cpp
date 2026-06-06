#include "history_store.hpp"

#include <sstream>

namespace catlight {

std::filesystem::path history_dir() {
  return data_root() / "history";
}

std::filesystem::path history_event_log_path() {
  return history_dir() / "events.jsonl";
}

std::filesystem::path history_sessions_path() {
  return history_dir() / "sessions.json";
}

std::filesystem::path history_database_path() {
  return history_dir() / "cat-light.sqlite";
}

std::string history_event_key(const AgentEvent &event) {
  std::vector<std::string> parts = {
      event.provider,
      event.session_id,
      event.instance_id,
      event.source,
      event.project_path,
      std::to_string(event.pid),
      format_iso_utc(event.timestamp),
      agent_state_text(event.state),
      event.phase,
      event.raw_kind,
      event.tool_name,
      event.model,
      event.detail,
      std::to_string(event.tokens.input),
      std::to_string(event.tokens.output),
      std::to_string(event.tokens.cache_read),
      std::to_string(event.tokens.cache_write),
      std::to_string(event.tokens.reasoning),
      std::to_string(event.tokens.total),
      std::to_string(event.context.used),
      std::to_string(event.context.limit),
  };
  return join_strings(parts, "|");
}

Json history_event_envelope(const AgentEvent &event) {
  Json::object_type obj;
  obj["key"] = history_event_key(event);
  obj["event"] = agent_event_to_json(event);
  return Json(std::move(obj));
}

std::optional<AgentEvent> history_event_from_json_line(const std::string &line, std::string *key, std::string *error) {
  std::string parse_error;
  Json json = Json::parse(line, &parse_error);
  if (!parse_error.empty()) {
    if (error) {
      *error = parse_error;
    }
    return std::nullopt;
  }
  const Json *event_json = json.get("event");
  if (event_json && event_json->is_object()) {
    if (key && json.get("key") && json.get("key")->is_string()) {
      *key = json.get("key")->as_string();
    }
    return agent_event_from_json(*event_json, error);
  }
  auto event = agent_event_from_json(json, error);
  if (event && key) {
    *key = history_event_key(*event);
  }
  return event;
}

bool sqlite_history_available() {
#ifdef CAT_LIGHT_HAS_SQLITE
  return true;
#else
  return false;
#endif
}

std::unique_ptr<HistoryStore> open_history_store(const Options &options) {
  const std::string storage = options.storage_backend.empty() ? "auto" : to_lower(options.storage_backend);
  if (storage == "jsonl") {
    return open_jsonl_history_store();
  }
  if (storage == "sqlite") {
#ifdef CAT_LIGHT_HAS_SQLITE
    return open_sqlite_history_store();
#else
    throw std::runtime_error("SQLite history backend is not compiled in; rebuild with CAT_LIGHT_ENABLE_SQLITE=ON");
#endif
  }
#ifdef CAT_LIGHT_HAS_SQLITE
  return open_sqlite_history_store();
#else
  return open_jsonl_history_store();
#endif
}

} // namespace catlight
