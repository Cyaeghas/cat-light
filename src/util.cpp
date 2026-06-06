#include "util.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <Windows.h>
#include <direct.h>
#define timegm _mkgmtime
#else
#include <sys/stat.h>
#endif

namespace catlight {
namespace {

std::optional<TimePoint> time_from_epoch(double value) {
  if (value <= 0) {
    return std::nullopt;
  }
  if (value > 1000000000000.0) {
    auto ms = std::chrono::milliseconds(static_cast<long long>(value));
    return TimePoint(ms);
  }
  auto sec = std::chrono::seconds(static_cast<long long>(value));
  return TimePoint(sec);
}

std::optional<TimePoint> parse_rfc3339(std::string value) {
  value = trim(std::move(value));
  if (value.empty()) {
    return std::nullopt;
  }
  int offset_seconds = 0;
  if (!value.empty() && value.back() == 'Z') {
    value.pop_back();
  } else {
    for (size_t i = value.size(); i > 10; --i) {
      const char c = value[i - 1];
      if (c != '+' && c != '-') {
        continue;
      }
      const std::string offset = value.substr(i - 1);
      if (offset.size() != 6 || offset[3] != ':') {
        break;
      }
      if (!std::isdigit(static_cast<unsigned char>(offset[1])) ||
          !std::isdigit(static_cast<unsigned char>(offset[2])) ||
          !std::isdigit(static_cast<unsigned char>(offset[4])) ||
          !std::isdigit(static_cast<unsigned char>(offset[5]))) {
        break;
      }
      const int hours = (offset[1] - '0') * 10 + (offset[2] - '0');
      const int minutes = (offset[4] - '0') * 10 + (offset[5] - '0');
      if (hours > 23 || minutes > 59) {
        break;
      }
      offset_seconds = (hours * 60 + minutes) * 60;
      if (c == '-') {
        offset_seconds = -offset_seconds;
      }
      value = value.substr(0, i - 1);
      break;
    }
  }
  auto dot = value.find('.');
  if (dot != std::string::npos) {
    value = value.substr(0, dot);
  }
  std::tm tm{};
  std::istringstream in(value);
  in >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
  if (in.fail()) {
    in.clear();
    in.str(value);
    tm = {};
    in >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
  }
  if (in.fail()) {
    in.clear();
    in.str(value);
    tm = {};
    in >> std::get_time(&tm, "%Y-%m-%d");
  }
  if (in.fail()) {
    return std::nullopt;
  }
  time_t epoch = timegm(&tm);
  if (epoch <= 0) {
    return std::nullopt;
  }
  epoch -= offset_seconds;
  return Clock::from_time_t(epoch);
}

int hex_value(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

std::string base64url_decode(std::string input) {
  std::replace(input.begin(), input.end(), '-', '+');
  std::replace(input.begin(), input.end(), '_', '/');
  while (input.size() % 4 != 0) {
    input.push_back('=');
  }
  static constexpr char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::array<int, 256> table{};
  table.fill(-1);
  for (int i = 0; alphabet[i]; ++i) {
    table[static_cast<unsigned char>(alphabet[i])] = i;
  }
  std::string out;
  int val = 0;
  int bits = -8;
  for (unsigned char c : input) {
    if (c == '=') {
      break;
    }
    if (table[c] == -1) {
      return "";
    }
    val = (val << 6) + table[c];
    bits += 6;
    if (bits >= 0) {
      out.push_back(static_cast<char>((val >> bits) & 0xFF));
      bits -= 8;
    }
  }
  return out;
}

} // namespace

std::optional<std::string> env_var(const std::string &name) {
  const char *value = std::getenv(name.c_str());
  if (!value || !*value) {
    return std::nullopt;
  }
  return std::string(value);
}

std::filesystem::path home_dir() {
#ifdef _WIN32
  if (auto userprofile = env_var("USERPROFILE")) {
    return *userprofile;
  }
  if (auto drive = env_var("HOMEDRIVE")) {
    if (auto path = env_var("HOMEPATH")) {
      return *drive + *path;
    }
  }
#else
  if (auto home = env_var("HOME")) {
    return *home;
  }
#endif
  return std::filesystem::current_path();
}

std::filesystem::path cache_root() {
#ifdef _WIN32
  if (auto local = env_var("LOCALAPPDATA")) {
    return std::filesystem::path(*local) / "cat-light" / "cache";
  }
#elif defined(__APPLE__)
  return home_dir() / "Library" / "Caches" / "cat-light";
#else
  if (auto xdg = env_var("XDG_CACHE_HOME")) {
    return std::filesystem::path(*xdg) / "cat-light";
  }
#endif
  return home_dir() / ".cache" / "cat-light";
}

std::filesystem::path provider_cache_path(const std::string &provider) {
  return cache_root() / provider / "usage.json";
}

std::filesystem::path codex_auth_path() {
  if (auto codex_home = env_var("CODEX_HOME")) {
    return std::filesystem::path(*codex_home) / "auth.json";
  }
  return home_dir() / ".codex" / "auth.json";
}

std::filesystem::path claude_credentials_path() {
  if (auto config = env_var("CLAUDE_CONFIG_DIR")) {
    return std::filesystem::path(*config) / ".credentials.json";
  }
  return home_dir() / ".claude" / ".credentials.json";
}

bool file_exists(const std::filesystem::path &path) {
  std::error_code ec;
  return std::filesystem::is_regular_file(path, ec);
}

std::string read_text_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot read " + path.string());
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void write_text_file(const std::filesystem::path &path, const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw std::runtime_error("cannot write " + path.string());
  }
  out << text;
}

std::optional<Json> read_json_file(const std::filesystem::path &path, std::string *error) {
  try {
    std::string text = read_text_file(path);
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
      text.erase(0, 3);
    }
    std::string parse_error;
    Json json = Json::parse(text, &parse_error);
    if (!parse_error.empty()) {
      if (error) {
        *error = parse_error;
      }
      return std::nullopt;
    }
    return json;
  } catch (const std::exception &e) {
    if (error) {
      *error = e.what();
    }
    return std::nullopt;
  }
}

bool write_json_file(const std::filesystem::path &path, const Json &value, std::string *error) {
  try {
    write_text_file(path, value.dump(2) + "\n");
#ifndef _WIN32
    chmod(path.string().c_str(), S_IRUSR | S_IWUSR);
#endif
    return true;
  } catch (const std::exception &e) {
    if (error) {
      *error = e.what();
    }
    return false;
  }
}

std::optional<CacheEntry> read_cache(const std::filesystem::path &path, std::string *error) {
  auto json = read_json_file(path, error);
  if (!json || !json->is_object()) {
    return std::nullopt;
  }
  const Json *fetched = json->get("fetched_at");
  const Json *body = json->get("body");
  if (!fetched || !body) {
    if (error) {
      *error = "cache is missing fetched_at or body";
    }
    return std::nullopt;
  }
  auto fetched_at = parse_time_value(*fetched);
  if (!fetched_at) {
    if (error) {
      *error = "cache has invalid fetched_at";
    }
    return std::nullopt;
  }
  return CacheEntry{*fetched_at, *body};
}

bool write_cache(const std::filesystem::path &path, const Json &body, std::string *error) {
  Json::object_type envelope;
  envelope["fetched_at"] = format_iso_utc(Clock::now());
  envelope["body"] = body;
  return write_json_file(path, Json(std::move(envelope)), error);
}

bool cache_is_fresh(const CacheEntry &cache, std::chrono::seconds ttl) {
  auto age = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - cache.fetched_at);
  return age.count() >= 0 && age <= ttl;
}

std::string to_lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::string shell_quote(const std::filesystem::path &path) {
  std::string value = path.string();
#ifdef _WIN32
  std::string out = "\"";
  for (char c : value) {
    if (c == '"') {
      out += "\\\"";
    } else {
      out.push_back(c);
    }
  }
  out += "\"";
  return out;
#else
  std::string out = "'";
  for (char c : value) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out.push_back(c);
    }
  }
  out += "'";
  return out;
#endif
}

std::string url_encode(const std::string &value) {
  std::ostringstream out;
  out << std::hex << std::uppercase;
  for (unsigned char c : value) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out << c;
    } else {
      out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    }
  }
  return out.str();
}

std::string trim(std::string value) {
  auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
  auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c); }).base();
  if (begin >= end) {
    return "";
  }
  return std::string(begin, end);
}

std::string join_strings(const std::vector<std::string> &parts, const std::string &separator) {
  std::ostringstream out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) {
      out << separator;
    }
    out << parts[i];
  }
  return out.str();
}

std::string format_time(TimePoint value) {
  std::time_t t = Clock::to_time_t(value);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S UTC");
  return out.str();
}

std::string format_iso_utc(TimePoint value) {
  std::time_t t = Clock::to_time_t(value);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

std::string format_utc_date(TimePoint value) {
  std::time_t t = Clock::to_time_t(value);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%d");
  return out.str();
}

std::optional<TimePoint> parse_time_text(const std::string &value) {
  return parse_time_value(Json(value));
}

std::optional<TimePoint> parse_time_value(const Json &value) {
  if (value.is_number()) {
    return time_from_epoch(value.as_number());
  }
  if (value.is_string()) {
    const std::string text = trim(value.as_string());
    if (text.empty()) {
      return std::nullopt;
    }
    char *end = nullptr;
    double numeric = std::strtod(text.c_str(), &end);
    if (end && *end == '\0') {
      return time_from_epoch(numeric);
    }
    return parse_rfc3339(text);
  }
  return std::nullopt;
}

std::string countdown(std::optional<TimePoint> value, TimePoint now) {
  if (!value) {
    return "--";
  }
  auto diff = std::chrono::duration_cast<std::chrono::minutes>(*value - now);
  if (diff.count() <= 0) {
    return "now";
  }
  auto total = diff.count();
  auto days = total / (60 * 24);
  auto hours = (total / 60) % 24;
  auto minutes = total % 60;
  std::ostringstream out;
  if (days > 0) {
    out << days << "d " << hours << "h";
  } else {
    out << hours << "h ";
    if (minutes < 10) {
      out << '0';
    }
    out << minutes << "m";
  }
  return out.str();
}

std::string human_age(TimePoint value, TimePoint now) {
  auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - value);
  if (diff.count() < 60) {
    return std::to_string(std::max<long long>(0, diff.count())) + "s";
  }
  auto minutes = diff.count() / 60;
  if (minutes < 60) {
    return std::to_string(minutes) + "m";
  }
  auto hours = minutes / 60;
  if (hours < 48) {
    return std::to_string(hours) + "h";
  }
  return std::to_string(hours / 24) + "d";
}

int clamp_percent(int value) {
  return std::max(0, std::min(100, value));
}

int round_percent(double value) {
  if (value <= 0) {
    return 0;
  }
  if (value >= 100) {
    return 100;
  }
  return static_cast<int>(value + 0.5);
}

int elapsed_percent(std::optional<TimePoint> reset_at, std::chrono::seconds window, TimePoint now) {
  if (!reset_at || window.count() <= 0) {
    return 0;
  }
  auto remaining = std::chrono::duration_cast<std::chrono::seconds>(*reset_at - now);
  auto elapsed = window - remaining;
  if (elapsed.count() <= 0) {
    return 0;
  }
  return clamp_percent(static_cast<int>((elapsed.count() * 100) / window.count()));
}

std::string pace_label(int delta) {
  if (delta > 0) {
    return std::to_string(delta) + "pts ahead";
  }
  if (delta < 0) {
    return std::to_string(-delta) + "pts under";
  }
  return "on track";
}

std::optional<TimePoint> jwt_expiry(const std::string &token) {
  auto first = token.find('.');
  if (first == std::string::npos) {
    return std::nullopt;
  }
  auto second = token.find('.', first + 1);
  if (second == std::string::npos) {
    return std::nullopt;
  }
  std::string payload = base64url_decode(token.substr(first + 1, second - first - 1));
  if (payload.empty()) {
    return std::nullopt;
  }
  std::string error;
  Json claims = Json::parse(payload, &error);
  if (!error.empty()) {
    return std::nullopt;
  }
  if (const Json *exp = claims.get("exp")) {
    return parse_time_value(*exp);
  }
  return std::nullopt;
}

} // namespace catlight
