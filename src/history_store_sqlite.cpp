#include "history_store.hpp"

#include <sqlite3.h>

#include <sstream>

namespace catlight {
namespace {

class Statement {
public:
  Statement(sqlite3 *db, const char *sql) : db_(db) {
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
      throw std::runtime_error(sqlite3_errmsg(db_));
    }
  }

  ~Statement() {
    if (stmt_) {
      sqlite3_finalize(stmt_);
    }
  }

  sqlite3_stmt *get() {
    return stmt_;
  }

private:
  sqlite3 *db_ = nullptr;
  sqlite3_stmt *stmt_ = nullptr;
};

void exec(sqlite3 *db, const char *sql) {
  char *error = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
    std::string message = error ? error : "sqlite exec failed";
    sqlite3_free(error);
    throw std::runtime_error(message);
  }
}

void bind_text(sqlite3_stmt *stmt, int index, const std::string &value) {
  if (sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
    throw std::runtime_error("sqlite bind failed");
  }
}

class SQLiteHistoryStore : public HistoryStore {
public:
  SQLiteHistoryStore() {
    std::filesystem::create_directories(history_dir());
    if (sqlite3_open(history_database_path().string().c_str(), &db_) != SQLITE_OK) {
      std::string message = db_ ? sqlite3_errmsg(db_) : "cannot open sqlite database";
      if (db_) {
        sqlite3_close(db_);
      }
      db_ = nullptr;
      throw std::runtime_error(message);
    }
    exec(db_, "PRAGMA journal_mode=WAL;");
    exec(db_, "PRAGMA synchronous=NORMAL;");
    exec(db_, "CREATE TABLE IF NOT EXISTS events ("
              "key TEXT PRIMARY KEY,"
              "provider TEXT,"
              "session_id TEXT,"
              "instance_id TEXT,"
              "source TEXT,"
              "project_path TEXT,"
              "model TEXT,"
              "state TEXT,"
              "phase TEXT,"
              "raw_kind TEXT,"
              "timestamp TEXT,"
              "event_json TEXT NOT NULL"
              ");");
    exec(db_, "CREATE TABLE IF NOT EXISTS sessions ("
              "instance_id TEXT PRIMARY KEY,"
              "provider TEXT,"
              "session_id TEXT,"
              "last_activity TEXT,"
              "session_json TEXT NOT NULL"
              ");");
  }

  ~SQLiteHistoryStore() override {
    if (db_) {
      sqlite3_close(db_);
    }
  }

  std::string name() const override {
    return "sqlite";
  }

  std::filesystem::path event_path() const override {
    return history_database_path();
  }

  std::filesystem::path sessions_path() const override {
    return history_database_path();
  }

  HistoryWriteResult append_events(const std::vector<AgentEvent> &events) override {
    HistoryWriteResult result;
    exec(db_, "BEGIN IMMEDIATE;");
    try {
      Statement stmt(db_, "INSERT OR IGNORE INTO events "
                          "(key, provider, session_id, instance_id, source, project_path, model, state, phase, raw_kind, timestamp, event_json) "
                          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
      for (const auto &event : events) {
        sqlite3_reset(stmt.get());
        sqlite3_clear_bindings(stmt.get());
        bind_text(stmt.get(), 1, history_event_key(event));
        bind_text(stmt.get(), 2, event.provider);
        bind_text(stmt.get(), 3, event.session_id);
        bind_text(stmt.get(), 4, event.instance_id);
        bind_text(stmt.get(), 5, event.source);
        bind_text(stmt.get(), 6, event.project_path);
        bind_text(stmt.get(), 7, event.model);
        bind_text(stmt.get(), 8, agent_state_text(event.state));
        bind_text(stmt.get(), 9, event.phase);
        bind_text(stmt.get(), 10, event.raw_kind);
        bind_text(stmt.get(), 11, format_iso_utc(event.timestamp));
        bind_text(stmt.get(), 12, agent_event_to_json(event).dump());
        if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
          throw std::runtime_error(sqlite3_errmsg(db_));
        }
        if (sqlite3_changes(db_) > 0) {
          ++result.inserted;
        } else {
          ++result.skipped;
        }
      }
      exec(db_, "COMMIT;");
    } catch (...) {
      exec(db_, "ROLLBACK;");
      throw;
    }
    return result;
  }

  std::vector<AgentEvent> read_events(std::string *error) override {
    std::vector<AgentEvent> events;
    Statement stmt(db_, "SELECT event_json FROM events ORDER BY timestamp, key;");
    while (true) {
      int rc = sqlite3_step(stmt.get());
      if (rc == SQLITE_DONE) {
        break;
      }
      if (rc != SQLITE_ROW) {
        throw std::runtime_error(sqlite3_errmsg(db_));
      }
      const unsigned char *text = sqlite3_column_text(stmt.get(), 0);
      std::string parse_error;
      Json json = Json::parse(reinterpret_cast<const char *>(text ? text : reinterpret_cast<const unsigned char *>("")), &parse_error);
      if (!parse_error.empty()) {
        if (error && error->empty()) {
          *error = parse_error;
        }
        continue;
      }
      std::string event_error;
      if (auto event = agent_event_from_json(json, &event_error)) {
        events.push_back(*event);
      } else if (error && error->empty()) {
        *error = event_error;
      }
    }
    return events;
  }

  void write_sessions(const std::vector<AgentSession> &sessions) override {
    exec(db_, "BEGIN IMMEDIATE;");
    try {
      exec(db_, "DELETE FROM sessions;");
      Statement stmt(db_, "INSERT INTO sessions "
                          "(instance_id, provider, session_id, last_activity, session_json) "
                          "VALUES (?, ?, ?, ?, ?);");
      for (const auto &session : sessions) {
        sqlite3_reset(stmt.get());
        sqlite3_clear_bindings(stmt.get());
        bind_text(stmt.get(), 1, session.instance_id);
        bind_text(stmt.get(), 2, session.provider);
        bind_text(stmt.get(), 3, session.session_id);
        bind_text(stmt.get(), 4, format_iso_utc(session.last_activity));
        bind_text(stmt.get(), 5, agent_session_to_json(session).dump());
        if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
          throw std::runtime_error(sqlite3_errmsg(db_));
        }
      }
      exec(db_, "COMMIT;");
    } catch (...) {
      exec(db_, "ROLLBACK;");
      throw;
    }
  }

private:
  sqlite3 *db_ = nullptr;
};

} // namespace

std::unique_ptr<HistoryStore> open_sqlite_history_store() {
  return std::make_unique<SQLiteHistoryStore>();
}

} // namespace catlight
