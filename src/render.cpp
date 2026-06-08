#include "render.hpp"

#include <sstream>

namespace catlight {
namespace {

Json window_to_json(const UsageWindow &window, TimePoint now) {
  Json::object_type obj;
  obj["name"] = window.name;
  obj["used_percent"] = window.used_percent;
  obj["remaining_percent"] = window.remaining_percent;
  obj["elapsed_percent"] = window.elapsed_percent;
  obj["pace_delta"] = window.pace_delta;
  obj["pace"] = window.pace;
  obj["resets_in"] = countdown(window.reset_at, now);
  if (window.reset_at) {
    obj["reset_at"] = format_iso_utc(*window.reset_at);
  }
  if (window.window.count() > 0) {
    obj["window_seconds"] = static_cast<double>(window.window.count());
  }
  return Json(std::move(obj));
}

Json credits_to_json(const Credits &credits) {
  Json::object_type obj;
  if (!credits.balance.empty()) {
    obj["balance"] = credits.balance;
  }
  if (!credits.extra_spent.empty()) {
    obj["extra_spent"] = credits.extra_spent;
  }
  if (!credits.extra_limit.empty()) {
    obj["extra_limit"] = credits.extra_limit;
  }
  obj["extra_percent"] = credits.extra_percent;
  return Json(std::move(obj));
}

Json status_to_json(const ProviderStatus &status, TimePoint now) {
  Json::object_type obj;
  obj["provider"] = status.key;
  obj["display_name"] = status.display_name;
  obj["state"] = provider_state_text(status.state);
  obj["message"] = status.message;
  obj["plan"] = status.plan;
  obj["credential_path"] = status.credential_path.string();
  obj["cache_path"] = status.cache_path.string();
  obj["from_cache"] = status.from_cache;
  obj["stale"] = status.stale;
  obj["severity"] = severity_for_percent(max_percent(status));
  obj["max_percent"] = max_percent(status);
  if (status.updated_at) {
    obj["updated_at"] = format_iso_utc(*status.updated_at);
    obj["age"] = human_age(*status.updated_at, now);
  }
  Json::array_type windows;
  for (const auto &window : status.windows) {
    windows.push_back(window_to_json(window, now));
  }
  obj["windows"] = Json(std::move(windows));
  if (status.credits) {
    obj["credits"] = credits_to_json(*status.credits);
  }
  return Json(std::move(obj));
}

std::string compact_status(const ProviderStatus &status, TimePoint now) {
  std::ostringstream out;
  out << status.display_name;
  if (status.windows.empty()) {
    out << " " << provider_state_text(status.state);
    return out.str();
  }
  const auto &window = status.windows.front();
  out << " " << window.used_percent << "%";
  if (window.reset_at) {
    out << " " << countdown(window.reset_at, now);
  }
  return out.str();
}

std::string detailed_status(const ProviderStatus &status, TimePoint now) {
  std::ostringstream out;
  out << status.display_name << " [" << provider_state_text(status.state) << "]";
  if (!status.plan.empty()) {
    out << " " << status.plan;
  }
  if (status.from_cache) {
    out << " cached";
  }
  if (status.stale) {
    out << " stale";
  }
  if (!status.message.empty()) {
    out << " - " << status.message;
  }
  for (const auto &window : status.windows) {
    out << "\n  " << window.name << ": " << window.used_percent << "% used";
    out << ", resets in " << countdown(window.reset_at, now);
    if (window.elapsed_percent > 0) {
      out << ", elapsed " << window.elapsed_percent << "%, " << window.pace;
    }
  }
  if (status.credits) {
    out << "\n  extra: " << status.credits->extra_percent << "%";
    if (!status.credits->extra_spent.empty() || !status.credits->extra_limit.empty()) {
      out << " (" << status.credits->extra_spent << "/" << status.credits->extra_limit << ")";
    }
    if (!status.credits->balance.empty()) {
      out << ", balance " << status.credits->balance;
    }
  }
  return out.str();
}

} // namespace

Json snapshot_to_json(const Snapshot &snapshot) {
  Json::object_type obj;
  obj["generated_at"] = format_iso_utc(snapshot.generated_at);
  obj["severity"] = overall_severity(snapshot);
  Json::array_type statuses;
  for (const auto &status : snapshot.statuses) {
    statuses.push_back(status_to_json(status, snapshot.generated_at));
  }
  obj["statuses"] = Json(std::move(statuses));
  return Json(std::move(obj));
}

Json doctor_to_json(const std::vector<DoctorCheck> &checks) {
  Json::array_type items;
  for (const auto &check : checks) {
    Json::object_type obj;
    obj["provider"] = check.provider;
    obj["display_name"] = check.display_name;
    obj["credential_label"] = check.credential_label;
    obj["credential_path"] = check.credential_path.string();
    obj["credential_ok"] = check.credential_ok;
    obj["cache_label"] = check.cache_label;
    obj["cache_path"] = check.cache_path.string();
    obj["cache_ok"] = check.cache_ok;
    obj["message"] = check.message;
    items.push_back(Json(std::move(obj)));
  }
  return Json(std::move(items));
}

std::string render_status_text(const Snapshot &snapshot) {
  std::ostringstream out;
  for (size_t i = 0; i < snapshot.statuses.size(); ++i) {
    if (i) {
      out << "\n";
    }
    out << detailed_status(snapshot.statuses[i], snapshot.generated_at);
  }
  if (snapshot.statuses.empty()) {
    out << "No providers selected.";
  }
  return out.str();
}

std::string render_waybar_json(const Snapshot &snapshot) {
  std::vector<std::string> parts;
  std::vector<std::string> tooltips;
  for (const auto &status : snapshot.statuses) {
    parts.push_back(compact_status(status, snapshot.generated_at));
    tooltips.push_back(detailed_status(status, snapshot.generated_at));
  }

  Json::object_type obj;
  obj["text"] = parts.empty() ? "cat-light --" : join_strings(parts, " | ");
  obj["tooltip"] = tooltips.empty() ? "No providers selected." : join_strings(tooltips, "\n\n");
  obj["class"] = overall_severity(snapshot);
  return Json(std::move(obj)).dump();
}

std::string render_doctor_text(const std::vector<DoctorCheck> &checks) {
  std::ostringstream out;
  for (const auto &check : checks) {
    out << check.display_name << ": ";
    if (check.provider == "curl") {
      out << (check.credential_ok ? "ok" : "missing");
    } else {
      out << check.credential_label << " " << (check.credential_ok ? "ok" : "missing");
      if (!check.cache_label.empty() && !check.cache_path.empty()) {
        out << ", " << check.cache_label << " " << (check.cache_ok ? "present" : "empty");
      }
      if (!check.credential_path.empty()) {
        out << "\n  " << check.credential_label << ": " << check.credential_path.string();
      }
      if (!check.cache_path.empty()) {
        out << "\n  " << check.cache_label << ": " << check.cache_path.string();
      }
    }
    if (!check.message.empty()) {
      out << "\n  " << check.message;
    }
    out << "\n";
  }
  return out.str();
}

std::string render_help() {
  return R"(cat-light 0.2.0

Usage:
  cat-light status [options]
  cat-light json [options]
  cat-light waybar [options]
  cat-light state [options]
  cat-light sessions [options]
  cat-light sessions-json [options]
  cat-light agent-waybar [options]
  cat-light sync [options]
  cat-light sync-json [options]
  cat-light history [options]
  cat-light history-json [options]
  cat-light history-summary [options]
  cat-light history-summary-json [options]
  cat-light history-trends-json [options]
  cat-light event --provider <codex|claude> --state <state> [event options]
  cat-light event --stdin
  cat-light hook-install [--provider <all|codex|claude>] [--shell <powershell|pwsh|sh>]
  cat-light hook-uninstall [--provider <all|codex|claude>]
  cat-light hook-status [--provider <all|codex|claude>]
  cat-light hook-script [--provider <codex|claude>] [--shell <powershell|pwsh|sh>]
  cat-light doctor [options]
  cat-light serve [options] [--addr 127.0.0.1:8750]

Options:
  -p, --provider <all|codex|claude>
  --offline              Use cache only.
  --refresh              Ignore fresh cache and fetch now.
  --no-token-refresh     Do not refresh Codex OAuth tokens.
  --timeout <seconds>    Network timeout, default 20.
  --max-sessions <n>     Maximum sessions to show, default 20.
  --cache-ttl <seconds>  Cache TTL, default 60.
  --storage <auto|jsonl|sqlite>
                         History storage backend, default auto.
  --since <time>         Filter history from YYYY-MM-DD, RFC3339, or epoch seconds.
  --until <time>         Filter history before time; date-only values include that UTC day.
  --days <n>             Filter history to the last n days.
  --shell <name>         Hook script shell: powershell, pwsh, or sh.
  --dry-run              Print hook install paths and snippets without writing scripts.
  -h, --help

Event options:
  --provider <codex|claude>
  --source <hook|api|cli>
  --session-id <id>
  --instance-id <id>
  --project-path <path>
  --pid <pid>
  --model <model>
  --state <starting|thinking|working|waiting|complete|error|idle|stale>
  --phase <phase>
  --detail <text>
  --tool-name <name>
  --raw-kind <kind>
  --input-tokens <n>
  --output-tokens <n>
  --cache-read-tokens <n>
  --cache-write-tokens <n>
  --reasoning-tokens <n>
  --total-tokens <n>
  --context-used <n>
  --context-limit <n>
  --context-percent <n>
)";
}

} // namespace catlight
