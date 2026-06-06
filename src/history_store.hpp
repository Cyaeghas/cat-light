#pragma once

#include "agent.hpp"
#include "app.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace catlight {

struct HistoryWriteResult {
  int inserted = 0;
  int skipped = 0;
};

class HistoryStore {
public:
  virtual ~HistoryStore() = default;
  virtual std::string name() const = 0;
  virtual std::filesystem::path event_path() const = 0;
  virtual std::filesystem::path sessions_path() const = 0;
  virtual HistoryWriteResult append_events(const std::vector<AgentEvent> &events) = 0;
  virtual std::vector<AgentEvent> read_events(std::string *error = nullptr) = 0;
  virtual void write_sessions(const std::vector<AgentSession> &sessions) = 0;
};

std::filesystem::path history_dir();
std::filesystem::path history_event_log_path();
std::filesystem::path history_sessions_path();
std::filesystem::path history_database_path();

std::string history_event_key(const AgentEvent &event);
Json history_event_envelope(const AgentEvent &event);
std::optional<AgentEvent> history_event_from_json_line(const std::string &line,
                                                       std::string *key = nullptr,
                                                       std::string *error = nullptr);

bool sqlite_history_available();
std::unique_ptr<HistoryStore> open_history_store(const Options &options);
std::unique_ptr<HistoryStore> open_jsonl_history_store();

#ifdef CAT_LIGHT_HAS_SQLITE
std::unique_ptr<HistoryStore> open_sqlite_history_store();
#endif

} // namespace catlight
