#include "sync.hpp"

#include "agent_scan.hpp"
#include "history_store.hpp"

#include <algorithm>
#include <map>
#include <sstream>
#include <set>

namespace catlight {
namespace {

std::vector<AgentEvent> collect_sync_candidates(const Options &options) {
  std::vector<AgentEvent> events;
  std::string read_error;
  auto manual = read_agent_events(&read_error);
  events.insert(events.end(), manual.begin(), manual.end());
  auto scanned = scan_agent_event_history(options);
  events.insert(events.end(), scanned.begin(), scanned.end());
  return events;
}

std::vector<AgentEvent> filter_provider(const std::vector<AgentEvent> &events, const Options &options) {
  std::vector<AgentEvent> out;
  for (const auto &event : events) {
    if (provider_selected(options, event.provider)) {
      out.push_back(event);
    }
  }
  return out;
}

bool in_history_range(const AgentEvent &event, const Options &options) {
  if (options.history_since && event.timestamp < *options.history_since) {
    return false;
  }
  if (options.history_until && event.timestamp >= *options.history_until) {
    return false;
  }
  return true;
}

std::vector<AgentEvent> filter_history_events(const std::vector<AgentEvent> &events, const Options &options) {
  std::vector<AgentEvent> out;
  for (const auto &event : events) {
    if (provider_selected(options, event.provider) && in_history_range(event, options)) {
      out.push_back(event);
    }
  }
  return out;
}

void add_tokens(TokenUsage &target, const TokenUsage &value) {
  target.input += value.input;
  target.output += value.output;
  target.cache_read += value.cache_read;
  target.cache_write += value.cache_write;
  target.reasoning += value.reasoning;
  target.total += value.total;
}

void keep_context_max(ContextUsage &target, const ContextUsage &value) {
  if (value.used > target.used) {
    target.used = value.used;
  }
  if (value.limit > target.limit) {
    target.limit = value.limit;
  }
  if (value.percent > target.percent) {
    target.percent = value.percent;
  }
}

std::string aggregate_key_or_unknown(const std::string &value) {
  return value.empty() ? "(unknown)" : value;
}

std::string session_identity(const AgentEvent &event) {
  if (!event.instance_id.empty()) {
    return event.instance_id;
  }
  std::string key = event.provider + ":";
  key += event.session_id.empty() ? "(unknown)" : event.session_id;
  if (event.pid > 0) {
    key += ":" + std::to_string(event.pid);
  }
  return key;
}

bool is_shell_tool(const std::string &tool) {
  const std::string lower = to_lower(tool);
  return lower == "shell_command" || lower == "bash" || lower == "shell" || lower == "terminal";
}

bool is_tool_success_event(const AgentEvent &event) {
  const std::string detail = to_lower(event.detail);
  return event.state != AgentState::Error &&
         (detail.find("completed") != std::string::npos || detail.find("complete") != std::string::npos ||
          detail.find("applied") != std::string::npos || detail.find("finished") != std::string::npos ||
          detail.find("received") != std::string::npos);
}

bool is_tool_failure_event(const AgentEvent &event) {
  const std::string detail = to_lower(event.detail);
  return event.state == AgentState::Error || detail.find("failed") != std::string::npos ||
         detail.find("error") != std::string::npos;
}

bool is_command_start_event(const AgentEvent &event) {
  if (!is_shell_tool(event.tool_name)) {
    return false;
  }
  const std::string detail = to_lower(event.detail);
  return detail.find("running command") != std::string::npos ||
         detail.find("waiting for approval") != std::string::npos;
}

bool is_command_output_event(const AgentEvent &event) {
  if (!is_shell_tool(event.tool_name)) {
    return false;
  }
  const std::string detail = to_lower(event.detail);
  return detail.find("command completed") != std::string::npos ||
         detail.find("command failed") != std::string::npos;
}

std::string command_key_from_event(const AgentEvent &event) {
  if (!is_shell_tool(event.tool_name)) {
    return "";
  }
  const std::string detail = trim(event.detail);
  const size_t open = detail.rfind('(');
  const size_t close = detail.rfind(')');
  if (open == std::string::npos || close == std::string::npos || close <= open + 1) {
    return "";
  }
  return trim(detail.substr(open + 1, close - open - 1));
}

std::string dimension_key(const std::string &value) {
  return value.empty() ? "(unknown)" : value;
}

void add_dimensions(HistoryAggregate &aggregate, const AgentEvent &event) {
  aggregate.providers[dimension_key(event.provider)]++;
  aggregate.models[dimension_key(event.model)]++;
  aggregate.projects[dimension_key(event.project_path)]++;
}

void add_dimensions(HistoryAggregate &aggregate, const AgentSession &session) {
  aggregate.providers[dimension_key(session.provider)]++;
  aggregate.models[dimension_key(session.model)]++;
  aggregate.projects[dimension_key(session.project_path)]++;
}

void add_duration(HistoryAggregate &aggregate, TimePoint start, TimePoint end) {
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
  if (seconds <= 0) {
    return;
  }
  aggregate.duration_seconds += seconds;
  aggregate.max_duration_seconds = std::max<long long>(aggregate.max_duration_seconds, seconds);
}

std::vector<HistoryAggregate> sorted_aggregates(const std::map<std::string, HistoryAggregate> &items) {
  std::vector<HistoryAggregate> out;
  for (const auto &entry : items) {
    out.push_back(entry.second);
  }
  std::sort(out.begin(), out.end(), [](const HistoryAggregate &a, const HistoryAggregate &b) {
    if (a.tokens.total != b.tokens.total) {
      return a.tokens.total > b.tokens.total;
    }
    if (a.events != b.events) {
      return a.events > b.events;
    }
    return a.key < b.key;
  });
  return out;
}

Json aggregate_to_json(const HistoryAggregate &aggregate) {
  Json::object_type obj;
  obj["key"] = aggregate.key;
  obj["events"] = aggregate.events;
  obj["sessions"] = aggregate.sessions;
  obj["successes"] = aggregate.successes;
  obj["failures"] = aggregate.failures;
  obj["duration_seconds"] = static_cast<double>(aggregate.duration_seconds);
  obj["max_duration_seconds"] = static_cast<double>(aggregate.max_duration_seconds);
  obj["tokens"] = token_usage_to_json(aggregate.tokens);
  obj["context"] = context_usage_to_json(aggregate.context);
  auto map_to_array = [](const std::map<std::string, int> &items) {
    std::vector<std::pair<std::string, int>> sorted(items.begin(), items.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
      if (a.second != b.second) {
        return a.second > b.second;
      }
      return a.first < b.first;
    });
    Json::array_type array;
    for (const auto &entry : sorted) {
      Json::object_type item;
      item["key"] = entry.first;
      item["events"] = entry.second;
      array.push_back(Json(std::move(item)));
    }
    return Json(std::move(array));
  };
  obj["providers"] = map_to_array(aggregate.providers);
  obj["models"] = map_to_array(aggregate.models);
  obj["projects"] = map_to_array(aggregate.projects);
  return Json(std::move(obj));
}

Json daily_to_json(const HistoryDailyAggregate &day) {
  Json::object_type obj;
  obj["date"] = day.date;
  obj["events"] = day.events;
  obj["sessions"] = day.sessions;
  obj["tokens"] = token_usage_to_json(day.tokens);
  obj["context"] = context_usage_to_json(day.context);
  return Json(std::move(obj));
}

Json history_range_to_json(std::optional<TimePoint> since, std::optional<TimePoint> until) {
  Json::object_type obj;
  obj["since"] = since ? Json(format_iso_utc(*since)) : Json(nullptr);
  obj["until"] = until ? Json(format_iso_utc(*until)) : Json(nullptr);
  return Json(std::move(obj));
}

std::string format_token_total(long long value) {
  if (value >= 1000000) {
    return std::to_string(value / 1000000) + "m";
  }
  if (value >= 1000) {
    return std::to_string(value / 1000) + "k";
  }
  return std::to_string(value);
}

void set_tool_sessions(std::map<std::string, HistoryAggregate> &groups,
                       const std::map<std::string, std::set<std::string>> &sessions) {
  for (auto &entry : groups) {
    if (auto found = sessions.find(entry.first); found != sessions.end()) {
      entry.second.sessions = static_cast<int>(found->second.size());
    }
  }
}

struct PendingCommand {
  std::string key;
  TimePoint started_at;
};

} // namespace

SyncStats sync_history(const Options &options) {
  auto store = open_history_store(options);
  SyncStats stats;
  stats.backend = store->name();
  stats.event_path = store->event_path();
  stats.sessions_path = store->sessions_path();

  auto events = collect_sync_candidates(options);
  stats.scanned = static_cast<int>(events.size());
  auto written = store->append_events(events);
  stats.inserted = written.inserted;
  stats.skipped = written.skipped;

  std::string read_error;
  auto sessions = merge_agent_events(store->read_events(&read_error));
  if (static_cast<int>(sessions.size()) > options.max_sessions) {
    sessions.resize(static_cast<size_t>(options.max_sessions));
  }
  store->write_sessions(sessions);
  return stats;
}

std::vector<AgentEvent> read_history_events(const Options &options, std::string *error) {
  return open_history_store(options)->read_events(error);
}

std::vector<AgentEvent> read_history_events(std::string *error) {
  return read_history_events(Options{}, error);
}

std::vector<AgentSession> read_history_sessions(const Options &options) {
  auto events = filter_history_events(read_history_events(options), options);
  auto sessions = merge_agent_events(events);
  if (static_cast<int>(sessions.size()) > options.max_sessions) {
    sessions.resize(static_cast<size_t>(options.max_sessions));
  }
  return sessions;
}

Json sync_stats_to_json(const SyncStats &stats) {
  Json::object_type obj;
  obj["backend"] = stats.backend;
  obj["scanned"] = stats.scanned;
  obj["inserted"] = stats.inserted;
  obj["skipped"] = stats.skipped;
  obj["event_path"] = stats.event_path.string();
  obj["sessions_path"] = stats.sessions_path.string();
  return Json(std::move(obj));
}

std::string render_sync_text(const SyncStats &stats) {
  std::ostringstream out;
  out << "Sync complete: " << stats.inserted << " inserted, " << stats.skipped << " skipped, "
      << stats.scanned << " scanned";
  out << "\n  backend:  " << stats.backend;
  out << "\n  events:   " << stats.event_path.string();
  out << "\n  sessions: " << stats.sessions_path.string();
  return out.str();
}

std::string render_history_text(const Options &options) {
  auto sessions = read_history_sessions(options);
  if (sessions.empty()) {
    return "No synced history found. Run cat-light sync first.";
  }
  return render_agent_sessions_text(sessions);
}

Json history_to_json(const Options &options) {
  Json::object_type obj;
  std::string error;
  auto store = open_history_store(options);
  auto events = filter_history_events(store->read_events(&error), options);
  auto sessions = read_history_sessions(options);
  obj["generated_at"] = format_iso_utc(Clock::now());
  obj["backend"] = store->name();
  obj["range"] = history_range_to_json(options.history_since, options.history_until);
  obj["event_count"] = static_cast<double>(events.size());
  obj["event_path"] = store->event_path().string();
  if (!error.empty()) {
    obj["warning"] = error;
  }
  Json::array_type items;
  for (const auto &session : sessions) {
    items.push_back(agent_session_to_json(session));
  }
  obj["sessions"] = Json(std::move(items));
  return Json(std::move(obj));
}

HistorySummary summarize_history(const Options &options) {
  HistorySummary summary;
  auto store = open_history_store(options);
  summary.backend = store->name();
  summary.since = options.history_since;
  summary.until = options.history_until;
  auto events = filter_history_events(store->read_events(), options);
  std::map<std::string, HistoryAggregate> providers;
  std::map<std::string, HistoryAggregate> models;
  std::map<std::string, HistoryAggregate> projects;
  std::map<std::string, HistoryAggregate> tools;
  std::map<std::string, HistoryAggregate> commands;
  std::map<std::string, HistoryDailyAggregate> daily;
  std::map<std::string, int> provider_events;
  std::map<std::string, int> model_events;
  std::map<std::string, int> project_events;
  std::map<std::string, std::set<std::string>> tool_sessions;
  std::map<std::string, std::set<std::string>> command_sessions;
  std::map<std::string, PendingCommand> pending_commands;
  auto daily_bucket = [&](TimePoint timestamp) -> HistoryDailyAggregate & {
    const std::string date = format_utc_date(timestamp);
    HistoryDailyAggregate &bucket = daily[date];
    bucket.date = date;
    return bucket;
  };

  for (const auto &event : events) {
    ++summary.events;
    ++daily_bucket(event.timestamp).events;
    const std::string provider_key = aggregate_key_or_unknown(event.provider);
    const std::string model_key = aggregate_key_or_unknown(event.model);
    const std::string project_key = aggregate_key_or_unknown(event.project_path);
    provider_events[provider_key]++;
    model_events[model_key]++;
    project_events[project_key]++;
    if (!event.tool_name.empty()) {
      const std::string tool_key = aggregate_key_or_unknown(event.tool_name);
      HistoryAggregate &aggregate = tools[tool_key];
      aggregate.key = tool_key;
      ++aggregate.events;
      if (is_tool_success_event(event)) {
        ++aggregate.successes;
      }
      if (is_tool_failure_event(event)) {
        ++aggregate.failures;
      }
      add_dimensions(aggregate, event);
      tool_sessions[tool_key].insert(session_identity(event));
    }
    const std::string identity = session_identity(event);
    if (is_command_start_event(event)) {
      const std::string command_key = command_key_from_event(event);
      if (!command_key.empty()) {
        HistoryAggregate &aggregate = commands[command_key];
        aggregate.key = command_key;
        ++aggregate.events;
        add_dimensions(aggregate, event);
        command_sessions[command_key].insert(identity);
        pending_commands[identity] = PendingCommand{command_key, event.timestamp};
      }
    } else if (is_command_output_event(event)) {
      auto pending = pending_commands.find(identity);
      if (pending == pending_commands.end()) {
        continue;
      }
      const std::string command_key = pending->second.key;
      HistoryAggregate &aggregate = commands[command_key];
      aggregate.key = command_key;
      if (is_tool_failure_event(event)) {
        ++aggregate.failures;
      } else {
        ++aggregate.successes;
      }
      add_duration(aggregate, pending->second.started_at, event.timestamp);
      pending_commands.erase(pending);
    }
  }

  auto sessions = merge_agent_events(events);
  summary.sessions = static_cast<int>(sessions.size());
  for (const auto &session : sessions) {
    add_tokens(summary.tokens, session.tokens);
    keep_context_max(summary.context, session.context);
    const std::string provider_key = aggregate_key_or_unknown(session.provider);
    const std::string model_key = aggregate_key_or_unknown(session.model);
    const std::string project_key = aggregate_key_or_unknown(session.project_path);
    auto update = [&](std::map<std::string, HistoryAggregate> &groups, const std::string &key) {
      HistoryAggregate &aggregate = groups[key];
      aggregate.key = key;
      ++aggregate.sessions;
      add_tokens(aggregate.tokens, session.tokens);
      keep_context_max(aggregate.context, session.context);
      add_dimensions(aggregate, session);
    };
    update(providers, provider_key);
    update(models, model_key);
    update(projects, project_key);
    HistoryDailyAggregate &day = daily_bucket(session.last_activity);
    ++day.sessions;
    add_tokens(day.tokens, session.tokens);
    keep_context_max(day.context, session.context);
  }

  for (auto &entry : providers) {
    entry.second.events = provider_events[entry.first];
  }
  for (auto &entry : models) {
    entry.second.events = model_events[entry.first];
  }
  for (auto &entry : projects) {
    entry.second.events = project_events[entry.first];
  }
  set_tool_sessions(tools, tool_sessions);
  set_tool_sessions(commands, command_sessions);
  summary.providers = sorted_aggregates(providers);
  summary.models = sorted_aggregates(models);
  summary.projects = sorted_aggregates(projects);
  summary.tools = sorted_aggregates(tools);
  summary.commands = sorted_aggregates(commands);
  for (const auto &entry : daily) {
    summary.daily.push_back(entry.second);
  }
  return summary;
}

Json history_summary_to_json(const HistorySummary &summary) {
  Json::object_type obj;
  obj["generated_at"] = format_iso_utc(Clock::now());
  obj["backend"] = summary.backend;
  obj["range"] = history_range_to_json(summary.since, summary.until);
  obj["events"] = summary.events;
  obj["sessions"] = summary.sessions;
  obj["tokens"] = token_usage_to_json(summary.tokens);
  obj["context"] = context_usage_to_json(summary.context);
  auto add_array = [](const std::vector<HistoryAggregate> &items) {
    Json::array_type array;
    for (const auto &item : items) {
      array.push_back(aggregate_to_json(item));
    }
    return Json(std::move(array));
  };
  obj["providers"] = add_array(summary.providers);
  obj["models"] = add_array(summary.models);
  obj["projects"] = add_array(summary.projects);
  obj["tools"] = add_array(summary.tools);
  obj["commands"] = add_array(summary.commands);
  Json::array_type daily;
  for (const auto &day : summary.daily) {
    daily.push_back(daily_to_json(day));
  }
  obj["daily"] = Json(std::move(daily));
  return Json(std::move(obj));
}

Json history_trends_to_json(const Options &options) {
  const HistorySummary summary = summarize_history(options);
  Json::object_type obj;
  obj["generated_at"] = format_iso_utc(Clock::now());
  obj["backend"] = summary.backend;
  obj["range"] = history_range_to_json(summary.since, summary.until);
  obj["events"] = summary.events;
  obj["sessions"] = summary.sessions;
  obj["tokens"] = token_usage_to_json(summary.tokens);
  obj["context"] = context_usage_to_json(summary.context);
  Json::array_type daily;
  for (const auto &day : summary.daily) {
    daily.push_back(daily_to_json(day));
  }
  obj["daily"] = Json(std::move(daily));
  return Json(std::move(obj));
}

std::string render_history_summary_text(const HistorySummary &summary) {
  std::ostringstream out;
  out << "History summary [" << summary.backend << "]";
  if (summary.since || summary.until) {
    out << "\n  range: ";
    out << (summary.since ? format_iso_utc(*summary.since) : "start");
    out << " to ";
    out << (summary.until ? format_iso_utc(*summary.until) : "now");
  }
  out << "\n  events: " << summary.events << ", sessions: " << summary.sessions
      << ", tokens: " << format_token_total(summary.tokens.total);
  auto render_group = [&](const std::string &title, const std::vector<HistoryAggregate> &items) {
    out << "\n\n" << title;
    if (items.empty()) {
      out << "\n  --";
      return;
    }
    for (size_t i = 0; i < items.size() && i < 8; ++i) {
      const auto &item = items[i];
      out << "\n  " << item.key << ": " << format_token_total(item.tokens.total)
          << " tokens, " << item.sessions << " sessions, " << item.events << " events";
      if (item.successes > 0 || item.failures > 0) {
        out << ", " << item.successes << " ok, " << item.failures << " failed";
      }
      if (item.duration_seconds > 0) {
        out << ", " << item.duration_seconds << "s";
      }
    }
  };
  render_group("Providers", summary.providers);
  render_group("Models", summary.models);
  render_group("Projects", summary.projects);
  render_group("Tools", summary.tools);
  render_group("Commands", summary.commands);
  return out.str();
}

} // namespace catlight
