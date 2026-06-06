#include "server.hpp"

#include "agent.hpp"
#include "agent_scan.hpp"
#include "render.hpp"
#include "sync.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
#endif

namespace catlight {
namespace {

void close_socket(socket_t socket) {
#ifdef _WIN32
  closesocket(socket);
#else
  close(socket);
#endif
}

bool parse_addr(const std::string &addr, std::string &host, unsigned short &port) {
  auto colon = addr.rfind(':');
  if (colon == std::string::npos) {
    return false;
  }
  host = addr.substr(0, colon);
  try {
    int parsed = std::stoi(addr.substr(colon + 1));
    if (parsed <= 0 || parsed > 65535) {
      return false;
    }
    port = static_cast<unsigned short>(parsed);
    return true;
  } catch (...) {
    return false;
  }
}

std::string http_response(const std::string &body, const std::string &content_type, int status = 200) {
  std::string status_text =
      status == 200 ? "OK" : status == 400 ? "Bad Request" : status == 404 ? "Not Found" : "Internal Server Error";
  std::ostringstream out;
  out << "HTTP/1.1 " << status << " " << status_text << "\r\n";
  out << "Content-Type: " << content_type << "\r\n";
  out << "Content-Length: " << body.size() << "\r\n";
  out << "Cache-Control: no-store\r\n";
  out << "Connection: close\r\n\r\n";
  out << body;
  return out.str();
}

std::string dashboard_html() {
  return R"(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>cat-light</title>
  <style>
    :root {
      color-scheme: light dark;
      --bg: #f6f7f9;
      --fg: #16181d;
      --muted: #5c6470;
      --line: #d7dce2;
      --panel: #ffffff;
      --ok: #2e7d4f;
      --mid: #b7791f;
      --high: #b5472f;
      --critical: #a32735;
      --track: #e7eaee;
    }
    @media (prefers-color-scheme: dark) {
      :root {
        --bg: #111318;
        --fg: #edf0f4;
        --muted: #a4adba;
        --line: #2b3038;
        --panel: #191d24;
        --track: #2a3038;
      }
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      background: var(--bg);
      color: var(--fg);
      font: 14px/1.45 system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    }
    main {
      width: min(980px, calc(100vw - 32px));
      margin: 24px auto;
    }
    header {
      display: flex;
      align-items: baseline;
      justify-content: space-between;
      gap: 16px;
      border-bottom: 1px solid var(--line);
      padding-bottom: 12px;
      margin-bottom: 16px;
    }
    h1 {
      font-size: 22px;
      margin: 0;
      letter-spacing: 0;
    }
    .meta { color: var(--muted); font-size: 13px; }
    .toolbar {
      display: flex;
      justify-content: space-between;
      align-items: center;
      flex-wrap: wrap;
      gap: 12px;
      margin-bottom: 14px;
    }
    .tabs, .actions {
      display: flex;
      gap: 8px;
      align-items: center;
      flex-wrap: wrap;
    }
    button, select {
      appearance: none;
      border: 1px solid var(--line);
      background: var(--panel);
      color: var(--fg);
      border-radius: 6px;
      padding: 6px 10px;
      font: inherit;
    }
    button {
      cursor: pointer;
    }
    button.active {
      border-color: var(--fg);
      font-weight: 650;
    }
    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
      gap: 12px;
    }
    section {
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 14px;
    }
    .provider {
      display: flex;
      justify-content: space-between;
      gap: 12px;
      align-items: center;
      margin-bottom: 12px;
    }
    h2 {
      font-size: 16px;
      margin: 0;
      letter-spacing: 0;
    }
    .state {
      border: 1px solid var(--line);
      border-radius: 999px;
      padding: 2px 8px;
      font-size: 12px;
      color: var(--muted);
    }
    .window { margin: 12px 0; }
    .row {
      display: flex;
      justify-content: space-between;
      gap: 10px;
      margin-bottom: 6px;
    }
    .name { font-weight: 650; }
    .value { color: var(--muted); }
    .bar {
      height: 10px;
      background: var(--track);
      border-radius: 999px;
      overflow: hidden;
    }
    .fill {
      height: 100%;
      width: 0%;
      background: var(--ok);
    }
    .fill.mid { background: var(--mid); }
    .fill.high { background: var(--high); }
    .fill.critical { background: var(--critical); }
    .fill.waiting { background: var(--high); }
    .fill.working, .fill.thinking, .fill.starting { background: var(--mid); }
    .fill.complete, .fill.idle, .fill.stale { background: var(--ok); }
    .detail {
      color: var(--muted);
      margin: 8px 0;
      overflow-wrap: anywhere;
    }
    .kv {
      display: grid;
      grid-template-columns: auto 1fr;
      gap: 4px 10px;
      color: var(--muted);
      font-size: 13px;
    }
    .metric {
      font-size: 26px;
      font-weight: 700;
      line-height: 1.15;
      margin-top: 4px;
    }
    .filters {
      display: flex;
      align-items: center;
      gap: 6px;
      color: var(--muted);
      font-size: 13px;
    }
    .filters select {
      min-width: 104px;
    }
    [hidden] {
      display: none !important;
    }
    .trend {
      display: grid;
      gap: 8px;
    }
    .trend-row {
      display: grid;
      grid-template-columns: 86px minmax(96px, 1fr) 74px;
      gap: 10px;
      align-items: center;
      min-height: 22px;
      color: var(--muted);
      font-size: 13px;
    }
    .trend-track {
      height: 9px;
      min-width: 0;
      background: var(--track);
      border-radius: 999px;
      overflow: hidden;
    }
    .trend-fill {
      height: 100%;
      width: 0%;
      min-width: 3px;
      background: var(--ok);
    }
    .trend-value {
      text-align: right;
      white-space: nowrap;
    }
    .message {
      color: var(--muted);
      border-top: 1px solid var(--line);
      padding-top: 10px;
      margin-top: 10px;
      overflow-wrap: anywhere;
    }
    pre {
      white-space: pre-wrap;
      overflow-wrap: anywhere;
      color: var(--muted);
      margin: 0;
    }
    @media (max-width: 560px) {
      main { width: min(980px, calc(100vw - 20px)); margin: 14px auto; }
      header { align-items: flex-start; flex-direction: column; }
      .toolbar { align-items: stretch; }
      .tabs, .actions { width: 100%; }
      .actions { justify-content: space-between; }
      .trend-row { grid-template-columns: 74px minmax(72px, 1fr) 64px; gap: 8px; }
    }
  </style>
</head>
<body>
  <main>
    <header>
      <h1>cat-light</h1>
      <div class="meta" id="meta">Loading</div>
    </header>
    <div class="toolbar">
      <div class="tabs">
        <button class="active" id="sessions-tab" type="button">Sessions</button>
        <button id="history-tab" type="button">History</button>
      </div>
      <div class="actions">
        <label class="filters" id="history-filters" hidden>
          <span>Range</span>
          <select id="history-range">
            <option value="">All</option>
            <option value="7">7 days</option>
            <option value="30" selected>30 days</option>
            <option value="90">90 days</option>
          </select>
        </label>
        <button id="sync-button" type="button">Sync</button>
      </div>
    </div>
    <div class="grid" id="grid"></div>
  </main>
  <script>
    const grid = document.getElementById('grid');
    const meta = document.getElementById('meta');
    const sessionsTab = document.getElementById('sessions-tab');
    const historyTab = document.getElementById('history-tab');
    const syncButton = document.getElementById('sync-button');
    const historyFilters = document.getElementById('history-filters');
    const historyRange = document.getElementById('history-range');
    let mode = 'sessions';
    const severity = value => value >= 90 ? 'critical' : value >= 75 ? 'high' : value >= 50 ? 'mid' : 'low';
    const escapeHtml = value => String(value ?? '').replace(/[&<>"']/g, c => ({
      '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
    }[c]));
    const formatTokens = value => {
      value = Number(value || 0);
      if (value >= 1000000) return `${Math.floor(value / 1000000)}m`;
      if (value >= 1000) return `${Math.floor(value / 1000)}k`;
      return value ? String(value) : '--';
    };
    function setMode(next) {
      mode = next;
      sessionsTab.classList.toggle('active', mode === 'sessions');
      historyTab.classList.toggle('active', mode === 'history');
      historyFilters.hidden = mode !== 'history';
      tick();
    }
    function historyQuery() {
      const params = new URLSearchParams();
      if (historyRange.value) params.set('days', historyRange.value);
      const query = params.toString();
      return query ? `?${query}` : '';
    }
    function historyRangeLabel() {
      return historyRange.options[historyRange.selectedIndex]?.textContent || 'All';
    }
    function renderQuota(snapshot) {
      meta.textContent = `Updated ${snapshot.generated_at} · ${snapshot.severity}`;
      grid.innerHTML = '';
      for (const status of snapshot.statuses) {
        const section = document.createElement('section');
        const windows = status.windows.map(w => {
          const level = severity(w.used_percent);
          return `<div class="window">
            <div class="row"><span class="name">${w.name}</span><span class="value">${w.used_percent}% · ${w.resets_in}</span></div>
            <div class="bar"><div class="fill ${level}" style="width:${w.used_percent}%"></div></div>
          </div>`;
        }).join('');
        section.innerHTML = `<div class="provider">
          <h2>${status.display_name}</h2>
          <span class="state">${status.state}</span>
        </div>${windows || '<pre>No usage windows</pre>'}${status.message ? `<div class="message">${status.message}</div>` : ''}`;
        grid.appendChild(section);
      }
    }
    function renderSessions(snapshot) {
      const sessions = snapshot.sessions || [];
      const active = sessions.filter(s => ['starting', 'thinking', 'working', 'waiting', 'error'].includes(s.state)).length;
      meta.textContent = `Updated ${snapshot.generated_at} | ${active} active / ${sessions.length} sessions`;
      grid.innerHTML = '';
      if (!sessions.length) {
        const section = document.createElement('section');
        section.innerHTML = '<div class="provider"><h2>Agents</h2><span class="state">idle</span></div><pre>No agent sessions found</pre>';
        grid.appendChild(section);
        return;
      }
      for (const session of sessions) {
        const section = document.createElement('section');
        const provider = session.provider === 'codex' ? 'Codex' : session.provider === 'claude' ? 'Claude' : 'Agent';
        const shortId = String(session.session_id || '').slice(0, 12) || 'default';
        const tokens = session.tokens || {};
        const context = session.context || {};
        const contextPercent = Math.max(0, Math.min(100, Number(context.percent || 0)));
        const contextText = context.limit ? `${contextPercent}% (${context.used || 0}/${context.limit})` : `${contextPercent}%`;
        section.innerHTML = `<div class="provider">
          <h2>${escapeHtml(provider)} ${escapeHtml(shortId)}</h2>
          <span class="state">${escapeHtml(session.state || 'idle')}</span>
        </div>
        ${session.detail ? `<div class="detail">${escapeHtml(session.detail)}</div>` : ''}
        <div class="kv">
          <span>model</span><span>${escapeHtml(session.model || '--')}</span>
          <span>project</span><span>${escapeHtml(session.project_path || '--')}</span>
          <span>last</span><span>${escapeHtml(session.age || '--')} ago</span>
          <span>tokens</span><span>${escapeHtml(formatTokens(tokens.total))}</span>
        </div>
        <div class="window">
          <div class="row"><span class="name">Context</span><span class="value">${escapeHtml(contextText)}</span></div>
          <div class="bar"><div class="fill ${escapeHtml(session.state || 'idle')}" style="width:${contextPercent}%"></div></div>
        </div>`;
        grid.appendChild(section);
      }
    }
    function renderGroup(title, items) {
      const section = document.createElement('section');
      const rows = (items || []).slice(0, 8).map(item => {
        const tokens = Number(item.tokens?.total || 0);
        const sessions = Number(item.sessions || 0);
        const events = Number(item.events || 0);
        const value = tokens > 0 ? `${formatTokens(tokens)} tokens | ${sessions} sessions` : `${events} events | ${sessions} sessions`;
        return `<div class="row">
          <span class="name">${escapeHtml(item.key)}</span>
          <span class="value">${escapeHtml(value)}</span>
        </div>`;
      }).join('');
      section.innerHTML = `<div class="provider"><h2>${escapeHtml(title)}</h2><span class="state">${Number((items || []).length)}</span></div>${rows || '<pre>--</pre>'}`;
      grid.appendChild(section);
    }
    function renderTrend(days) {
      const section = document.createElement('section');
      const shown = (days || []).slice(-14);
      const maxValue = Math.max(1, ...shown.map(day => Number(day.tokens?.total || day.events || 0)));
      const rows = shown.map(day => {
        const value = Number(day.tokens?.total || 0);
        const fallback = Number(day.events || 0);
        const width = Math.max(3, Math.round(((value || fallback) / maxValue) * 100));
        return `<div class="trend-row">
          <span>${escapeHtml(day.date || '--')}</span>
          <div class="trend-track"><div class="trend-fill" style="width:${width}%"></div></div>
          <span class="trend-value">${escapeHtml(formatTokens(value))}</span>
        </div>`;
      }).join('');
      section.innerHTML = `<div class="provider"><h2>Daily Trend</h2><span class="state">${shown.length}</span></div>
        <div class="trend">${rows || '<pre>--</pre>'}</div>`;
      grid.appendChild(section);
    }
    function renderHistory(summary) {
      meta.textContent = `History ${summary.backend || 'jsonl'} | ${historyRangeLabel()} | ${summary.events || 0} events / ${summary.sessions || 0} sessions`;
      grid.innerHTML = '';
      const total = document.createElement('section');
      total.innerHTML = `<div class="provider"><h2>Total Tokens</h2><span class="state">${escapeHtml(summary.backend || 'jsonl')}</span></div>
        <div class="metric">${escapeHtml(formatTokens(summary.tokens?.total))}</div>
        <div class="kv">
          <span>input</span><span>${escapeHtml(formatTokens(summary.tokens?.input))}</span>
          <span>output</span><span>${escapeHtml(formatTokens(summary.tokens?.output))}</span>
          <span>reasoning</span><span>${escapeHtml(formatTokens(summary.tokens?.reasoning))}</span>
          <span>context</span><span>${escapeHtml(summary.context?.percent || 0)}%</span>
        </div>`;
      grid.appendChild(total);
      renderTrend(summary.daily);
      renderGroup('Providers', summary.providers);
      renderGroup('Models', summary.models);
      renderGroup('Projects', summary.projects);
      renderGroup('Tools', summary.tools);
      renderGroup('Commands', summary.commands);
    }
    async function tick() {
      try {
        const response = await fetch(mode === 'history' ? `/history-summary${historyQuery()}` : '/state', {cache: 'no-store'});
        const payload = await response.json();
        if (mode === 'history') {
          renderHistory(payload);
        } else {
          renderSessions(payload);
        }
      } catch (error) {
        meta.textContent = String(error);
      }
    }
    sessionsTab.addEventListener('click', () => setMode('sessions'));
    historyTab.addEventListener('click', () => setMode('history'));
    syncButton.addEventListener('click', async () => {
      syncButton.disabled = true;
      try {
        const response = await fetch('/sync', {method: 'POST', cache: 'no-store'});
        const stats = await response.json();
        meta.textContent = `Sync ${stats.inserted || 0} inserted / ${stats.skipped || 0} skipped`;
        await tick();
      } catch (error) {
        meta.textContent = String(error);
      } finally {
        syncButton.disabled = false;
      }
    });
    historyRange.addEventListener('change', tick);
    tick();
    setInterval(tick, 5000);
  </script>
</body>
</html>
)";
}

int query_hex_value(char c) {
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

std::string url_decode_query(std::string value) {
  std::string out;
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '+') {
      out.push_back(' ');
    } else if (value[i] == '%' && i + 2 < value.size()) {
      const int hi = query_hex_value(value[i + 1]);
      const int lo = query_hex_value(value[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
      } else {
        out.push_back(value[i]);
      }
    } else {
      out.push_back(value[i]);
    }
  }
  return out;
}

std::map<std::string, std::string> parse_query_string(const std::string &query) {
  std::map<std::string, std::string> values;
  size_t start = 0;
  while (start <= query.size()) {
    const size_t amp = query.find('&', start);
    const std::string item = amp == std::string::npos ? query.substr(start) : query.substr(start, amp - start);
    if (!item.empty()) {
      const size_t eq = item.find('=');
      std::string key = eq == std::string::npos ? item : item.substr(0, eq);
      std::string value = eq == std::string::npos ? "" : item.substr(eq + 1);
      values[to_lower(url_decode_query(key))] = url_decode_query(value);
    }
    if (amp == std::string::npos) {
      break;
    }
    start = amp + 1;
  }
  return values;
}

bool parse_int_query(const std::string &value, int &out) {
  if (value.empty()) {
    return false;
  }
  try {
    size_t pos = 0;
    int parsed = std::stoi(value, &pos);
    if (pos != value.size()) {
      return false;
    }
    out = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

bool is_query_date_only(const std::string &value) {
  const std::string text = trim(value);
  if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
    return false;
  }
  for (size_t i = 0; i < text.size(); ++i) {
    if (i == 4 || i == 7) {
      continue;
    }
    if (!std::isdigit(static_cast<unsigned char>(text[i]))) {
      return false;
    }
  }
  return true;
}

std::optional<TimePoint> parse_query_time(const std::string &value, bool end_of_day) {
  auto parsed = parse_time_text(value);
  if (!parsed) {
    return std::nullopt;
  }
  if (end_of_day && is_query_date_only(value)) {
    return *parsed + std::chrono::hours(24);
  }
  return parsed;
}

std::string json_error_body(const std::string &error) {
  Json::object_type obj;
  obj["ok"] = false;
  obj["error"] = error;
  return Json(std::move(obj)).dump();
}

bool apply_history_query_options(Options &options,
                                 const std::map<std::string, std::string> &query,
                                 std::string &error) {
  if (auto it = query.find("provider"); it != query.end() && !it->second.empty()) {
    const std::string provider = to_lower(it->second);
    if (provider != "all" && provider != "codex" && provider != "claude") {
      error = "provider must be all, codex, or claude";
      return false;
    }
    options.provider = provider;
  }
  if (auto it = query.find("max_sessions"); it != query.end() && !it->second.empty()) {
    int value = 0;
    if (!parse_int_query(it->second, value) || value <= 0) {
      error = "max_sessions must be a positive integer";
      return false;
    }
    options.max_sessions = value;
  }
  if (auto it = query.find("days"); it != query.end()) {
    const std::string value = to_lower(trim(it->second));
    if (value.empty() || value == "all") {
      options.history_since.reset();
      options.history_until.reset();
    } else {
      int days = 0;
      if (!parse_int_query(value, days) || days <= 0) {
        error = "days must be a positive integer or all";
        return false;
      }
      options.history_since = Clock::now() - std::chrono::hours(24 * days);
      options.history_until.reset();
    }
  }
  if (auto it = query.find("since"); it != query.end() && !it->second.empty()) {
    auto parsed = parse_query_time(it->second, false);
    if (!parsed) {
      error = "invalid since, expected YYYY-MM-DD, RFC3339, or epoch seconds";
      return false;
    }
    options.history_since = *parsed;
  }
  if (auto it = query.find("until"); it != query.end() && !it->second.empty()) {
    auto parsed = parse_query_time(it->second, true);
    if (!parsed) {
      error = "invalid until, expected YYYY-MM-DD, RFC3339, or epoch seconds";
      return false;
    }
    options.history_until = *parsed;
  }
  if (options.history_since && options.history_until && *options.history_since >= *options.history_until) {
    error = "history range must have since earlier than until";
    return false;
  }
  return true;
}

std::string request_target(const std::string &request) {
  std::istringstream in(request);
  std::string method;
  std::string target;
  in >> method >> target;
  return target.empty() ? "/" : target;
}

std::string request_path(const std::string &request) {
  std::string path = request_target(request);
  if (path.empty()) {
    return "/";
  }
  auto query = path.find('?');
  return query == std::string::npos ? path : path.substr(0, query);
}

std::string request_query(const std::string &request) {
  const std::string target = request_target(request);
  const size_t query = target.find('?');
  if (query == std::string::npos) {
    return "";
  }
  return target.substr(query + 1);
}

std::string request_method(const std::string &request) {
  std::istringstream in(request);
  std::string method;
  in >> method;
  return method;
}

std::string request_body(const std::string &request) {
  const std::string marker = "\r\n\r\n";
  auto pos = request.find(marker);
  if (pos == std::string::npos) {
    return "";
  }
  return request.substr(pos + marker.size());
}

size_t request_content_length(const std::string &request) {
  std::istringstream in(request);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      break;
    }
    auto colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    std::string key = to_lower(trim(line.substr(0, colon)));
    if (key == "content-length") {
      try {
        return static_cast<size_t>(std::stoull(trim(line.substr(colon + 1))));
      } catch (...) {
        return 0;
      }
    }
  }
  return 0;
}

void receive_remaining_body(socket_t client, std::string &request) {
  const size_t expected = request_content_length(request);
  if (expected == 0) {
    return;
  }
  while (request_body(request).size() < expected) {
    char buffer[8192]{};
#ifdef _WIN32
    int received = recv(client, buffer, static_cast<int>(sizeof(buffer)), 0);
#else
    ssize_t received = recv(client, buffer, sizeof(buffer), 0);
#endif
    if (received <= 0) {
      return;
    }
    request.append(buffer, static_cast<size_t>(received));
  }
}

void send_all(socket_t client, const std::string &data) {
  const char *ptr = data.data();
  size_t left = data.size();
  while (left > 0) {
#ifdef _WIN32
    int sent = send(client, ptr, static_cast<int>(left), 0);
#else
    ssize_t sent = send(client, ptr, left, 0);
#endif
    if (sent <= 0) {
      return;
    }
    ptr += sent;
    left -= static_cast<size_t>(sent);
  }
}

} // namespace

int run_server(const Options &options) {
#ifdef _WIN32
  WSADATA data{};
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    std::cerr << "WSAStartup failed\n";
    return 1;
  }
#endif

  std::string host;
  unsigned short port = 0;
  if (!parse_addr(options.addr, host, port)) {
    std::cerr << "invalid --addr, expected host:port\n";
    return 1;
  }

  socket_t server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (server == kInvalidSocket) {
    std::cerr << "cannot create socket\n";
    return 1;
  }

  int yes = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes), sizeof(yes));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
    std::cerr << "serve currently supports IPv4 addresses only\n";
    close_socket(server);
    return 1;
  }
  if (bind(server, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    std::cerr << "cannot bind " << options.addr << "\n";
    close_socket(server);
    return 1;
  }
  if (listen(server, 16) != 0) {
    std::cerr << "cannot listen on " << options.addr << "\n";
    close_socket(server);
    return 1;
  }

  std::cout << "cat-light serving http://" << options.addr << "\n";
  while (true) {
    socket_t client = accept(server, nullptr, nullptr);
    if (client == kInvalidSocket) {
      continue;
    }
    char buffer[8192]{};
#ifdef _WIN32
    int received = recv(client, buffer, static_cast<int>(sizeof(buffer) - 1), 0);
#else
    ssize_t received = recv(client, buffer, sizeof(buffer) - 1, 0);
#endif
    if (received <= 0) {
      close_socket(client);
      continue;
    }
    std::string request(buffer, static_cast<size_t>(received));
    receive_remaining_body(client, request);
    std::string method = request_method(request);
    std::string path = request_path(request);
    const auto query = parse_query_string(request_query(request));
    Options request_options = options;
    std::string query_error;
    std::string response;
    const bool history_route = path == "/history" || path == "/history-summary" || path == "/history-trends";
    if (history_route && !apply_history_query_options(request_options, query, query_error)) {
      response = http_response(json_error_body(query_error), "application/json; charset=utf-8", 400);
    } else if (path == "/" || path == "/index.html") {
      response = http_response(dashboard_html(), "text/html; charset=utf-8");
    } else if (path == "/usage") {
      auto snapshot = collect_snapshot(options);
      response = http_response(snapshot_to_json(snapshot).dump(), "application/json; charset=utf-8");
    } else if (path == "/state" || path == "/sessions") {
      response = http_response(agent_sessions_to_json(collect_agent_sessions(options)).dump(), "application/json; charset=utf-8");
    } else if (path == "/history") {
      response = http_response(history_to_json(request_options).dump(), "application/json; charset=utf-8");
    } else if (path == "/history-summary") {
      response = http_response(history_summary_to_json(summarize_history(request_options)).dump(), "application/json; charset=utf-8");
    } else if (path == "/history-trends") {
      response = http_response(history_trends_to_json(request_options).dump(), "application/json; charset=utf-8");
    } else if (path == "/sync" && (method == "POST" || method == "GET")) {
      response = http_response(sync_stats_to_json(sync_history(options)).dump(), "application/json; charset=utf-8");
    } else if (path == "/event" && method == "POST") {
      std::string parse_error;
      Json json = Json::parse(request_body(request), &parse_error);
      if (!parse_error.empty()) {
        Json::object_type err;
        err["ok"] = false;
        err["error"] = "invalid json";
        err["detail"] = parse_error;
        err["body_length"] = static_cast<double>(request_body(request).size());
        response = http_response(Json(std::move(err)).dump(), "application/json; charset=utf-8", 500);
      } else {
        std::string event_error;
        auto event = agent_event_from_json(json, &event_error);
        if (!event) {
          response = http_response("{\"ok\":false,\"error\":\"invalid event\"}", "application/json; charset=utf-8", 500);
        } else if (!append_agent_event(*event, &event_error)) {
          response = http_response("{\"ok\":false,\"error\":\"write failed\"}", "application/json; charset=utf-8", 500);
        } else {
          Json::object_type ok;
          ok["ok"] = true;
          ok["event"] = agent_event_to_json(*event);
          response = http_response(Json(std::move(ok)).dump(), "application/json; charset=utf-8");
        }
      }
    } else if (path == "/health") {
      response = http_response("{\"ok\":true}", "application/json; charset=utf-8");
    } else {
      response = http_response("not found", "text/plain; charset=utf-8", 404);
    }
    send_all(client, response);
    close_socket(client);
  }
}

} // namespace catlight
