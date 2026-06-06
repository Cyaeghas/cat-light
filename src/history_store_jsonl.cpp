#include "history_store.hpp"

#include <fstream>
#include <set>

namespace catlight {
namespace {

class JsonlHistoryStore : public HistoryStore {
public:
  std::string name() const override {
    return "jsonl";
  }

  std::filesystem::path event_path() const override {
    return history_event_log_path();
  }

  std::filesystem::path sessions_path() const override {
    return history_sessions_path();
  }

  HistoryWriteResult append_events(const std::vector<AgentEvent> &events) override {
    std::filesystem::create_directories(history_dir());
    HistoryWriteResult result;
    std::set<std::string> keys = read_keys();
    std::ofstream out(event_path(), std::ios::binary | std::ios::app);
    if (!out) {
      throw std::runtime_error("cannot open history event log");
    }
    for (const auto &event : events) {
      std::string key = history_event_key(event);
      if (keys.insert(key).second) {
        out << history_event_envelope(event).dump() << "\n";
        ++result.inserted;
      } else {
        ++result.skipped;
      }
    }
    return result;
  }

  std::vector<AgentEvent> read_events(std::string *error) override {
    std::vector<AgentEvent> events;
    std::ifstream in(event_path(), std::ios::binary);
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
      std::string key;
      std::string event_error;
      if (auto event = history_event_from_json_line(line, &key, &event_error)) {
        events.push_back(*event);
      } else if (error && error->empty()) {
        *error = "history parse failed at line " + std::to_string(line_no) + ": " + event_error;
      }
    }
    return events;
  }

  void write_sessions(const std::vector<AgentSession> &sessions) override {
    write_text_file(sessions_path(), agent_sessions_to_json(sessions).dump(2) + "\n");
  }

private:
  std::set<std::string> read_keys() {
    std::set<std::string> keys;
    std::ifstream in(event_path(), std::ios::binary);
    if (!in) {
      return keys;
    }
    std::string line;
    while (std::getline(in, line)) {
      line = trim(line);
      if (line.empty()) {
        continue;
      }
      std::string key;
      std::string error;
      if (auto event = history_event_from_json_line(line, &key, &error)) {
        keys.insert(key.empty() ? history_event_key(*event) : key);
      }
    }
    return keys;
  }
};

} // namespace

std::unique_ptr<HistoryStore> open_jsonl_history_store() {
  return std::make_unique<JsonlHistoryStore>();
}

} // namespace catlight
