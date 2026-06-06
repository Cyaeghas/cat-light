#include "app.hpp"

#include "http_client.hpp"

#include <sstream>

namespace catlight {
namespace {

constexpr const char *kUsageURL = "https://chatgpt.com/backend-api/wham/usage";
constexpr const char *kTokenURL = "https://auth.openai.com/oauth/token";
constexpr const char *kClientID = "app_EMoamEEZ73f0CkXaXp7hrann";

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

UsageWindow parse_codex_window(const Json &json,
                               std::string name,
                               std::chrono::seconds fallback_window,
                               TimePoint now) {
  UsageWindow window;
  window.name = std::move(name);
  window.window = fallback_window;

  if (auto seconds = number_key(json, {"window_seconds", "limit_window_seconds", "duration_seconds", "period_seconds"})) {
    window.window = std::chrono::seconds(static_cast<long long>(*seconds));
  }
  if (auto percent = number_key(json, {"used_percent", "usage_percent", "percent_used", "utilization", "usage"})) {
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

std::optional<Credits> parse_codex_credits(const Json &usage) {
  const Json *credits_json = usage.get("credits");
  if (!credits_json || !credits_json->is_object()) {
    credits_json = usage.get("credit");
  }
  if (!credits_json || !credits_json->is_object()) {
    return std::nullopt;
  }
  Credits credits;
  credits.balance = string_key(*credits_json, {"balance", "remaining", "available", "remaining_credits"});
  credits.extra_spent = string_key(*credits_json, {"extra_spent", "spent", "used"});
  credits.extra_limit = string_key(*credits_json, {"extra_limit", "limit", "total"});
  if (auto percent = number_key(*credits_json, {"used_percent", "usage_percent", "percent_used"})) {
    credits.extra_percent = round_percent(*percent <= 1.0 ? *percent * 100.0 : *percent);
  }
  if (credits.balance.empty() && credits.extra_spent.empty() && credits.extra_limit.empty() && credits.extra_percent == 0) {
    return std::nullopt;
  }
  return credits;
}

bool parse_codex_usage(const Json &usage, ProviderStatus &status, TimePoint fetched_at, std::string *error) {
  if (!usage.is_object()) {
    if (error) {
      *error = "Codex usage response is not an object";
    }
    return false;
  }
  status.state = ProviderState::Ok;
  status.updated_at = fetched_at;
  status.plan = string_key(usage, {"plan_type", "plan", "tier", "subscription_plan"});
  status.windows.clear();

  const Json *rate = usage.get("rate_limit");
  if (!rate || !rate->is_object()) {
    rate = usage.get("rate_limits");
  }
  if (!rate || !rate->is_object()) {
    rate = &usage;
  }

  const Json *primary = rate->get("primary_window");
  if (!primary || !primary->is_object()) {
    primary = rate->get("session");
  }
  if (!primary || !primary->is_object()) {
    primary = rate->get("five_hour");
  }
  if (primary && primary->is_object()) {
    status.windows.push_back(parse_codex_window(*primary, "session", std::chrono::hours(5), fetched_at));
  }

  const Json *secondary = rate->get("secondary_window");
  if (!secondary || !secondary->is_object()) {
    secondary = rate->get("weekly");
  }
  if (!secondary || !secondary->is_object()) {
    secondary = rate->get("seven_day");
  }
  if (secondary && secondary->is_object()) {
    status.windows.push_back(parse_codex_window(*secondary, "weekly", std::chrono::hours(24 * 7), fetched_at));
  }

  if (status.windows.empty()) {
    for (const auto &entry : rate->object()) {
      if (entry.second.is_object()) {
        auto name = entry.first;
        status.windows.push_back(parse_codex_window(entry.second, name, std::chrono::seconds(0), fetched_at));
      }
    }
  }

  status.credits = parse_codex_credits(usage);
  if (status.windows.empty() && !status.credits) {
    if (error) {
      *error = "Codex usage response did not contain recognizable usage windows";
    }
    return false;
  }
  return true;
}

bool load_cached_codex(ProviderStatus &status, bool stale, const std::string &message) {
  std::string cache_error;
  auto cache = read_cache(status.cache_path, &cache_error);
  if (!cache) {
    status.message = message.empty() ? cache_error : message;
    return false;
  }
  std::string parse_error;
  if (!parse_codex_usage(cache->body, status, cache->fetched_at, &parse_error)) {
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

bool refresh_codex_token(Json &auth,
                         const std::filesystem::path &auth_path,
                         std::string &access_token,
                         const Options &options,
                         std::string &message) {
  std::string refresh_token = path_string(auth, {"tokens", "refresh_token"});
  if (refresh_token.empty()) {
    refresh_token = path_string(auth, {"tokens", "refreshToken"});
  }
  if (refresh_token.empty()) {
    message = "Codex refresh token not found";
    return false;
  }

  std::string body = "grant_type=refresh_token";
  body += "&client_id=" + url_encode(kClientID);
  body += "&refresh_token=" + url_encode(refresh_token);
  body += "&scope=" + url_encode("openid profile email offline_access");

  HttpClient client(options.timeout_seconds);
  auto response = client.request("POST", kTokenURL,
                                 {{"Content-Type", "application/x-www-form-urlencoded"},
                                  {"Accept", "application/json"}},
                                 body);
  if (!response.ok()) {
    message = "Codex token refresh failed: " + response.error;
    return false;
  }

  std::string parse_error;
  Json token_json = Json::parse(response.body, &parse_error);
  if (!parse_error.empty() || !token_json.is_object()) {
    message = "Codex token refresh response parse failed: " + parse_error;
    return false;
  }

  std::string new_access = string_key(token_json, {"access_token", "accessToken"});
  std::string new_refresh = string_key(token_json, {"refresh_token", "refreshToken"});
  std::string new_id = string_key(token_json, {"id_token", "idToken"});
  if (new_access.empty()) {
    message = "Codex token refresh response did not include access_token";
    return false;
  }

  Json &tokens = auth["tokens"];
  tokens["access_token"] = new_access;
  if (!new_refresh.empty()) {
    tokens["refresh_token"] = new_refresh;
  }
  if (!new_id.empty()) {
    tokens["id_token"] = new_id;
  }
  auth["last_refresh"] = format_iso_utc(Clock::now());

  std::string write_error;
  if (!write_json_file(auth_path, auth, &write_error)) {
    message = "Codex token refreshed but auth.json write failed: " + write_error;
    return false;
  }

  access_token = new_access;
  return true;
}

std::optional<TimePoint> codex_access_expiry(const std::string &access_token, const Json &auth) {
  if (auto exp = jwt_expiry(access_token)) {
    return exp;
  }
  if (const Json *token_exp = auth.path({"tokens", "expires_at"})) {
    return parse_time_value(*token_exp);
  }
  if (const Json *token_exp = auth.path({"tokens", "expiresAt"})) {
    return parse_time_value(*token_exp);
  }
  return std::nullopt;
}

HttpResponse fetch_codex_usage(const std::string &access_token,
                               const std::string &account_id,
                               const Options &options) {
  std::map<std::string, std::string> headers = {
      {"Accept", "application/json"},
      {"Authorization", "Bearer " + access_token},
      {"User-Agent", "cat-light/0.1"}};
  if (!account_id.empty()) {
    headers["ChatGPT-Account-Id"] = account_id;
  }
  HttpClient client(options.timeout_seconds);
  return client.request("GET", kUsageURL, headers);
}

} // namespace

ProviderStatus fetch_codex_status(const Options &options) {
  ProviderStatus status;
  status.id = ProviderId::Codex;
  status.key = "codex";
  status.display_name = "Codex";
  status.credential_path = codex_auth_path();
  status.cache_path = provider_cache_path("codex");

  std::string cache_error;
  auto cache = read_cache(status.cache_path, &cache_error);
  if (!options.refresh && cache && cache_is_fresh(*cache, options.cache_ttl)) {
    std::string parse_error;
    if (parse_codex_usage(cache->body, status, cache->fetched_at, &parse_error)) {
      status.from_cache = true;
      return status;
    }
  }

  if (options.offline) {
    if (load_cached_codex(status, true, "offline; using cached Codex data")) {
      status.state = ProviderState::Offline;
      return status;
    }
    status.state = ProviderState::Offline;
    status.message = "offline and no Codex cache is available";
    return status;
  }

  std::string auth_error;
  auto auth = read_json_file(status.credential_path, &auth_error);
  if (!auth) {
    status.state = ProviderState::MissingAuth;
    status.message = "missing or invalid Codex auth file: " + auth_error;
    load_cached_codex(status, true, status.message);
    return status;
  }

  std::string access_token = path_string(*auth, {"tokens", "access_token"});
  if (access_token.empty()) {
    access_token = path_string(*auth, {"tokens", "accessToken"});
  }
  std::string account_id = path_string(*auth, {"tokens", "account_id"});
  if (account_id.empty()) {
    account_id = path_string(*auth, {"tokens", "accountId"});
  }
  if (access_token.empty()) {
    status.state = ProviderState::AuthError;
    status.message = "Codex access token not found";
    load_cached_codex(status, true, status.message);
    return status;
  }

  if (options.token_refresh) {
    if (auto expires = codex_access_expiry(access_token, *auth)) {
      if (*expires <= Clock::now() + std::chrono::minutes(5)) {
        std::string refresh_message;
        refresh_codex_token(*auth, status.credential_path, access_token, options, refresh_message);
      }
    }
  }

  auto response = fetch_codex_usage(access_token, account_id, options);
  if ((response.status == 401 || response.status == 403) && options.token_refresh) {
    std::string refresh_message;
    if (refresh_codex_token(*auth, status.credential_path, access_token, options, refresh_message)) {
      response = fetch_codex_usage(access_token, account_id, options);
    }
  }

  if (!response.ok()) {
    status.state = response.status == 401 || response.status == 403 ? ProviderState::AuthError : ProviderState::NetworkError;
    status.message = "Codex usage request failed: " + response.error;
    load_cached_codex(status, true, status.message);
    return status;
  }

  std::string parse_error;
  Json usage = Json::parse(response.body, &parse_error);
  if (!parse_error.empty()) {
    status.state = ProviderState::ParseError;
    status.message = "Codex usage parse failed: " + parse_error;
    load_cached_codex(status, true, status.message);
    return status;
  }

  if (!parse_codex_usage(usage, status, Clock::now(), &parse_error)) {
    status.state = ProviderState::ParseError;
    status.message = parse_error;
    load_cached_codex(status, true, status.message);
    return status;
  }

  std::string write_error;
  write_cache(status.cache_path, usage, &write_error);
  return status;
}

} // namespace catlight
