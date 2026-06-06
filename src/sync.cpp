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
  obj["tokens"] = token_usage_to_json(aggregate.tokens);
  obj["context"] = context_usage_to_json(aggregate.context);
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
  std::map<std::string, HistoryDailyAggregate> daily;
  std::map<std::string, int> provider_events;
  std::map<std::string, int> model_events;
  std::map<std::string, int> project_events;
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
  summary.providers = sorted_aggregates(providers);
  summary.models = sorted_aggregates(models);
  summary.projects = sorted_aggregates(projects);
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
    }
  };
  render_group("Providers", summary.providers);
  render_group("Models", summary.models);
  render_group("Projects", summary.projects);
  return out.str();
}

} // namespace catlight
