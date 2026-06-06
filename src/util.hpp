#pragma once

#include "json.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace catlight {

using Clock = std::chrono::system_clock;
using TimePoint = Clock::time_point;

struct CacheEntry {
  TimePoint fetched_at;
  Json body;
};

std::optional<std::string> env_var(const std::string &name);
std::filesystem::path home_dir();
std::filesystem::path cache_root();
std::filesystem::path provider_cache_path(const std::string &provider);
std::filesystem::path codex_auth_path();
std::filesystem::path claude_credentials_path();

bool file_exists(const std::filesystem::path &path);
std::string read_text_file(const std::filesystem::path &path);
void write_text_file(const std::filesystem::path &path, const std::string &text);

std::optional<Json> read_json_file(const std::filesystem::path &path, std::string *error = nullptr);
bool write_json_file(const std::filesystem::path &path, const Json &value, std::string *error = nullptr);

std::optional<CacheEntry> read_cache(const std::filesystem::path &path, std::string *error = nullptr);
bool write_cache(const std::filesystem::path &path, const Json &body, std::string *error = nullptr);
bool cache_is_fresh(const CacheEntry &cache, std::chrono::seconds ttl);

std::string to_lower(std::string value);
std::string shell_quote(const std::filesystem::path &path);
std::string url_encode(const std::string &value);
std::string trim(std::string value);
std::string join_strings(const std::vector<std::string> &parts, const std::string &separator);

std::string format_time(TimePoint value);
std::string format_iso_utc(TimePoint value);
std::string format_utc_date(TimePoint value);
std::optional<TimePoint> parse_time_text(const std::string &value);
std::optional<TimePoint> parse_time_value(const Json &value);
std::string countdown(std::optional<TimePoint> value, TimePoint now);
std::string human_age(TimePoint value, TimePoint now);
int clamp_percent(int value);
int round_percent(double value);
int elapsed_percent(std::optional<TimePoint> reset_at, std::chrono::seconds window, TimePoint now);
std::string pace_label(int delta);

std::optional<TimePoint> jwt_expiry(const std::string &token);

} // namespace catlight
