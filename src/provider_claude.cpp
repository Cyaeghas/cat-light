#include "app.hpp"

#include "http_client.hpp"

#include <sstream>

namespace catlight {
namespace {

constexpr const char *kUsageURL = "https://api.anthropic.com/api/oauth/usage";

std::string string_key(const Json &json, std::initializer_list<std::string> keys) {
  for (const auto &key : keys) {
    if (const Json *value = json.get(key)) {
      if (value->is_string()) {
        return value->as_string();
      }
      if (value->is_number()) {
        std::ostringstream out;
        out << static_cast<long long>(value->as_number());
        return out.str();
      }
    }
  }
  return "";
}

std::string path_string(const Json &json, std::initializer_list<std::string_view> path) {
  if (const Json *value = json.path(path)) {
    if (value->is_string()) {
      return value->as_string();
    }
  }
  return "";
}

std::optional<double> number_key(const Json &json, std::initializer_list<std::string> keys) {
  for (const auto &key : keys) {
    if (const Json *value = json.get(key)) {
      if (value->is_number()) {
        return value->as_number();
      }
      if (value->is_string()) {
        try {
          return std::stod(value->as_string());
        } catch (...) {
        }
      }
    }
  }
  return std::nullopt;
}

std::optional<TimePoint> time_key(const Json &json, std::initializer_list<std::string> keys) {
  for (const auto &key : keys) {
    if (const Json *value = json.get(key)) {
      if (auto parsed = parse_time_value(*value)) {
        return parsed;
      }
    }
  }
  return std::nullopt;
}

const Json *object_or_nested(const Json &root, const std::string &name) {
  if (const Json *direct = root.get(name); direct && direct->is_object()) {
    return direct;
  }
  if (const Json *limits = root.get("limits"); limits && limits->is_object()) {
    if (const Json *nested = limits->get(name); nested && nested->is_object()) {
      return nested;
    }
  }
  if (const Json *data = root.get("data"); data && data->is_object()) {
    if (const Json *nested = object_or_nested(*data, name)) {
      return nested;
    }
  }
  return nullptr;
}

UsageWindow parse_claude_window(const Json &json,
                                std::string name,
                                std::chrono::seconds fallback_window,
                                TimePoint now) {
  UsageWindow window;
  window.name = std::move(name);
  window.window = fallback_window;
  if (auto seconds = number_key(json, {"window_seconds", "duration_seconds", "period_seconds"})) {
    window.window = std::chrono::seconds(static_cast<long long>(*seconds));
  }
  if (auto percent = number_key(json, {"used_percent", "usage_percent", "percent_used", "utilization", "percent"})) {
    double value = *percent;
    if (value <= 1.0) {
      value *= 100.0;
    }
    window.used_percent = round_percent(value);
  } else {
    auto used = number_key(json, {"used", "current", "consumed"});
    auto limit = number_key(json, {"limit", "max", "quota"});
    if (used && limit && *limit > 0) {
      window.used_percent = round_percent((*used / *limit) * 100.0);
    }
  }
  window.reset_at = time_key(json, {"reset_at", "resets_at", "reset_time", "ends_at", "end_time"});
  enrich_window(window, now);
  return window;
}

std::optional<Credits> parse_claude_extra(const Json &usage) {
  const Json *extra = object_or_nested(usage, "extra_usage");
  if (!extra) {
    extra = object_or_nested(usage, "extras");
  }
  if (!extra) {
    return std::nullopt;
  }
  Credits credits;
  credits.extra_spent = string_key(*extra, {"spent", "used", "current"});
  credits.extra_limit = string_key(*extra, {"limit", "max", "quota"});
  if (auto percent = number_key(*extra, {"used_percent", "usage_percent", "percent_used", "utilization", "percent"})) {
    credits.extra_percent = round_percent(*percent <= 1.0 ? *percent * 100.0 : *percent);
  } else {
    auto spent = number_key(*extra, {"spent", "used", "current"});
    auto limit = number_key(*extra, {"limit", "max", "quota"});
    if (spent && limit && *limit > 0) {
      credits.extra_percent = round_percent((*spent / *limit) * 100.0);
    }
  }
  if (credits.extra_spent.empty() && credits.extra_limit.empty() && credits.extra_percent == 0) {
    return std::nullopt;
  }
  return credits;
}

bool parse_claude_usage(const Json &usage, ProviderStatus &status, TimePoint fetched_at, std::string *error) {
  if (!usage.is_object()) {
    if (error) {
      *error = "Claude usage response is not an object";
    }
    return false;
  }
  status.state = ProviderState::Ok;
  status.updated_at = fetched_at;
  status.plan = string_key(usage, {"plan", "plan_type", "subscription_plan", "tier"});
  status.windows.clear();

  if (const Json *five = object_or_nested(usage, "five_hour")) {
    status.windows.push_back(parse_claude_window(*five, "5h", std::chrono::hours(5), fetched_at));
  }
  if (const Json *seven = object_or_nested(usage, "seven_day")) {
    status.windows.push_back(parse_claude_window(*seven, "7d", std::chrono::hours(24 * 7), fetched_at));
  }
  if (const Json *sonnet = object_or_nested(usage, "sonnet")) {
    status.windows.push_back(parse_claude_window(*sonnet, "sonnet", std::chrono::hours(24 * 7), fetched_at));
  }
  if (const Json *opus = object_or_nested(usage, "opus")) {
    status.windows.push_back(parse_claude_window(*opus, "opus", std::chrono::hours(24 * 7), fetched_at));
  }

  if (status.windows.empty()) {
    const Json *limits = usage.get("limits");
    if (!limits || !limits->is_object()) {
      limits = &usage;
    }
    for (const auto &entry : limits->object()) {
      if (entry.second.is_object()) {
        auto name = entry.first;
        if (name == "extra_usage" || name == "extras") {
          continue;
        }
        status.windows.push_back(parse_claude_window(entry.second, name, std::chrono::seconds(0), fetched_at));
      }
    }
  }

  status.credits = parse_claude_extra(usage);
  if (status.windows.empty() && !status.credits) {
    if (error) {
      *error = "Claude usage response did not contain recognizable usage windows";
    }
    return false;
  }
  return true;
}

bool load_cached_claude(ProviderStatus &status, bool stale, const std::string &message) {
  std::string cache_error;
  auto cache = read_cache(status.cache_path, &cache_error);
  if (!cache) {
    status.message = message.empty() ? cache_error : message;
    return false;
  }
  std::string parse_error;
  if (!parse_claude_usage(cache->body, status, cache->fetched_at, &parse_error)) {
    status.message = parse_error;
    status.state = ProviderState::ParseError;
    return false;
  }
  status.from_cache = true;
  status.stale = stale;
  status.state = stale ? ProviderState::Stale : ProviderState::Ok;
  status.message = message;
  return true;
}

std::string claude_access_token(const Json &credentials) {
  for (auto path : {std::initializer_list<std::string_view>{"claudeAiOauth", "accessToken"},
                    std::initializer_list<std::string_view>{"claudeAiOauth", "access_token"},
                    std::initializer_list<std::string_view>{"oauth", "accessToken"},
                    std::initializer_list<std::string_view>{"oauth", "access_token"}}) {
    auto value = path_string(credentials, path);
    if (!value.empty()) {
      return value;
    }
  }
  if (const Json *value = credentials.get("accessToken"); value && value->is_string()) {
    return value->as_string();
  }
  if (const Json *value = credentials.get("access_token"); value && value->is_string()) {
    return value->as_string();
  }
  return "";
}

} // namespace

ProviderStatus fetch_claude_status(const Options &options) {
  ProviderStatus status;
  status.id = ProviderId::Claude;
  status.key = "claude";
  status.display_name = "Claude";
  status.credential_path = claude_credentials_path();
  status.cache_path = provider_cache_path("claude");

  std::string cache_error;
  auto cache = read_cache(status.cache_path, &cache_error);
  if (!options.refresh && cache && cache_is_fresh(*cache, options.cache_ttl)) {
    std::string parse_error;
    if (parse_claude_usage(cache->body, status, cache->fetched_at, &parse_error)) {
      status.from_cache = true;
      return status;
    }
  }

  if (options.offline) {
    if (load_cached_claude(status, true, "offline; using cached Claude data")) {
      status.state = ProviderState::Offline;
      return status;
    }
    status.state = ProviderState::Offline;
    status.message = "offline and no Claude cache is available";
    return status;
  }

  std::string credential_error;
  auto credentials = read_json_file(status.credential_path, &credential_error);
  if (!credentials) {
    status.state = ProviderState::MissingAuth;
    status.message = "missing or invalid Claude credentials file: " + credential_error;
    load_cached_claude(status, true, status.message);
    return status;
  }

  std::string access_token = claude_access_token(*credentials);
  if (access_token.empty()) {
    status.state = ProviderState::AuthError;
    status.message = "Claude access token not found";
    load_cached_claude(status, true, status.message);
    return status;
  }

  std::map<std::string, std::string> headers = {
      {"Accept", "application/json"},
      {"Authorization", "Bearer " + access_token},
      {"User-Agent", "cat-light/0.1"},
      {"anthropic-beta", "oauth-2025-04-20"},
      {"anthropic-version", "2023-06-01"}};
  HttpClient client(options.timeout_seconds);
  auto response = client.request("GET", kUsageURL, headers);
  if (!response.ok()) {
    status.state = response.status == 401 || response.status == 403 ? ProviderState::AuthError : ProviderState::NetworkError;
    status.message = "Claude usage request failed: " + response.error;
    load_cached_claude(status, true, status.message);
    return status;
  }

  std::string parse_error;
  Json usage = Json::parse(response.body, &parse_error);
  if (!parse_error.empty()) {
    status.state = ProviderState::ParseError;
    status.message = "Claude usage parse failed: " + parse_error;
    load_cached_claude(status, true, status.message);
    return status;
  }
  if (!parse_claude_usage(usage, status, Clock::now(), &parse_error)) {
    status.state = ProviderState::ParseError;
    status.message = parse_error;
    load_cached_claude(status, true, status.message);
    return status;
  }

  std::string write_error;
  write_cache(status.cache_path, usage, &write_error);
  return status;
}

} // namespace catlight
