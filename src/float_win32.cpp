#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cwctype>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr UINT_PTR kRefreshTimer = 1;
constexpr UINT_PTR kServerTimer = 2;
constexpr UINT kStatusReadyMessage = WM_APP + 10;
constexpr int kMenuOpen = 1001;
constexpr int kMenuSync = 1002;
constexpr int kMenuHookStatus = 1003;
constexpr int kMenuRefresh = 1004;
constexpr int kMenuResetPosition = 1005;
constexpr int kMenuQuit = 1006;
constexpr int kWindowWidth = 344;
constexpr int kWindowHeight = 184;
constexpr int kMissingPosition = -32000;

struct CommandResult {
  bool ok = false;
  bool timed_out = false;
  DWORD exit_code = 1;
  std::string output;
  std::wstring error;
};

struct FloatStatus {
  std::wstring summary = L"Loading agent sessions";
  std::wstring state_class = L"starting";
  std::vector<std::wstring> details = {L"Starting local monitor..."};
  SYSTEMTIME updated{};
  bool ok = false;
};

struct UiFonts {
  HFONT title = nullptr;
  HFONT row = nullptr;
  HFONT detail = nullptr;
  HFONT tiny = nullptr;
};

PROCESS_INFORMATION g_server{};
std::wstring g_cat_light;
std::mutex g_status_mutex;
FloatStatus g_status;
UiFonts g_fonts;
std::atomic<bool> g_refreshing = false;
std::atomic<bool> g_closing = false;

std::wstring quote(const std::wstring &value) {
  std::wstring out = L"\"";
  for (wchar_t ch : value) {
    if (ch == L'"') {
      out += L"\\\"";
    } else {
      out.push_back(ch);
    }
  }
  out += L"\"";
  return out;
}

std::wstring module_dir() {
  wchar_t path[MAX_PATH]{};
  DWORD written = GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring value(path, written);
  size_t slash = value.find_last_of(L"\\/");
  if (slash == std::wstring::npos) {
    return L".";
  }
  return value.substr(0, slash);
}

std::wstring local_app_data_dir() {
  DWORD needed = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
  if (needed == 0) {
    return module_dir();
  }
  std::wstring value(needed, L'\0');
  DWORD written = GetEnvironmentVariableW(L"LOCALAPPDATA", &value[0], needed);
  if (written == 0 || written >= needed) {
    return module_dir();
  }
  value.resize(written);
  return value;
}

std::wstring float_config_path() {
  std::wstring dir = local_app_data_dir() + L"\\cat-light";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir + L"\\float.ini";
}

bool file_exists(const std::wstring &path) {
  DWORD attrs = GetFileAttributesW(path.c_str());
  return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool process_alive(const PROCESS_INFORMATION &process) {
  if (!process.hProcess) {
    return false;
  }
  DWORD code = 0;
  return GetExitCodeProcess(process.hProcess, &code) && code == STILL_ACTIVE;
}

void close_handle(HANDLE &handle) {
  if (handle) {
    CloseHandle(handle);
    handle = nullptr;
  }
}

void close_process_handles(PROCESS_INFORMATION &process) {
  if (process.hThread) {
    CloseHandle(process.hThread);
    process.hThread = nullptr;
  }
  if (process.hProcess) {
    CloseHandle(process.hProcess);
    process.hProcess = nullptr;
  }
  process.dwProcessId = 0;
  process.dwThreadId = 0;
}

bool start_server() {
  if (process_alive(g_server)) {
    return true;
  }
  close_process_handles(g_server);
  if (!file_exists(g_cat_light)) {
    return false;
  }

  std::wstring command = quote(g_cat_light) + L" serve --addr 127.0.0.1:8750";
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;

  BOOL ok = CreateProcessW(nullptr,
                           &command[0],
                           nullptr,
                           nullptr,
                           FALSE,
                           CREATE_NO_WINDOW,
                           nullptr,
                           module_dir().c_str(),
                           &startup,
                           &g_server);
  return ok == TRUE;
}

void stop_server() {
  if (process_alive(g_server)) {
    TerminateProcess(g_server.hProcess, 0);
    WaitForSingleObject(g_server.hProcess, 2000);
  }
  close_process_handles(g_server);
}

void run_hidden(const std::wstring &args) {
  if (!file_exists(g_cat_light)) {
    return;
  }
  std::wstring command = quote(g_cat_light) + L" " + args;
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION process{};
  if (CreateProcessW(nullptr,
                     &command[0],
                     nullptr,
                     nullptr,
                     FALSE,
                     CREATE_NO_WINDOW,
                     nullptr,
                     module_dir().c_str(),
                     &startup,
                     &process)) {
    close_process_handles(process);
  }
}

void run_visible_console(const std::wstring &args) {
  if (!file_exists(g_cat_light)) {
    return;
  }
  std::wstring command = L"/k " + quote(g_cat_light) + L" " + args;
  ShellExecuteW(nullptr, L"open", L"cmd.exe", command.c_str(), module_dir().c_str(), SW_SHOWNORMAL);
}

void open_dashboard() {
  start_server();
  ShellExecuteW(nullptr, L"open", L"http://127.0.0.1:8750", nullptr, nullptr, SW_SHOWNORMAL);
}

CommandResult run_capture(const std::wstring &args, DWORD timeout_ms) {
  CommandResult result;
  if (!file_exists(g_cat_light)) {
    result.error = L"cat-light.exe was not found next to this window.";
    return result;
  }

  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.bInheritHandle = TRUE;

  HANDLE read_pipe = nullptr;
  HANDLE write_pipe = nullptr;
  if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) {
    result.error = L"Failed to create output pipe.";
    return result;
  }
  SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

  std::wstring command = quote(g_cat_light) + L" " + args;
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  startup.hStdOutput = write_pipe;
  startup.hStdError = write_pipe;
  startup.hStdInput = nullptr;
  startup.wShowWindow = SW_HIDE;

  PROCESS_INFORMATION process{};
  BOOL ok = CreateProcessW(nullptr,
                           &command[0],
                           nullptr,
                           nullptr,
                           TRUE,
                           CREATE_NO_WINDOW,
                           nullptr,
                           module_dir().c_str(),
                           &startup,
                           &process);
  close_handle(write_pipe);
  if (!ok) {
    close_handle(read_pipe);
    result.error = L"Failed to start cat-light.exe.";
    return result;
  }
  close_handle(process.hThread);

  const ULONGLONG started = GetTickCount64();
  std::array<char, 4096> buffer{};
  bool process_done = false;
  while (!process_done) {
    DWORD available = 0;
    if (!PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &available, nullptr)) {
      break;
    }
    while (available > 0) {
      DWORD to_read = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
      DWORD read = 0;
      if (!ReadFile(read_pipe, buffer.data(), to_read, &read, nullptr) || read == 0) {
        available = 0;
        break;
      }
      result.output.append(buffer.data(), buffer.data() + read);
      available -= read;
    }

    DWORD wait = WaitForSingleObject(process.hProcess, 25);
    process_done = wait == WAIT_OBJECT_0;
    if (!process_done && GetTickCount64() - started > timeout_ms) {
      result.timed_out = true;
      TerminateProcess(process.hProcess, 1);
      WaitForSingleObject(process.hProcess, 1000);
      process_done = true;
    }
  }

  for (;;) {
    DWORD available = 0;
    if (!PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &available, nullptr) || available == 0) {
      break;
    }
    DWORD to_read = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
    DWORD read = 0;
    if (!ReadFile(read_pipe, buffer.data(), to_read, &read, nullptr) || read == 0) {
      break;
    }
    result.output.append(buffer.data(), buffer.data() + read);
  }

  GetExitCodeProcess(process.hProcess, &result.exit_code);
  result.ok = !result.timed_out && result.exit_code == 0;
  close_handle(read_pipe);
  close_handle(process.hProcess);
  return result;
}

int hex_value(char ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f') {
    return ch - 'a' + 10;
  }
  if (ch >= 'A' && ch <= 'F') {
    return ch - 'A' + 10;
  }
  return -1;
}

void append_utf8(std::string &out, unsigned int codepoint) {
  if (codepoint <= 0x7F) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

bool parse_hex4(const std::string &text, size_t pos, unsigned int &value) {
  if (pos + 4 > text.size()) {
    return false;
  }
  value = 0;
  for (size_t i = 0; i < 4; ++i) {
    int digit = hex_value(text[pos + i]);
    if (digit < 0) {
      return false;
    }
    value = (value << 4) | static_cast<unsigned int>(digit);
  }
  return true;
}

bool json_string_field(const std::string &json, const std::string &key, std::string &out) {
  const std::string needle = "\"" + key + "\"";
  size_t pos = json.find(needle);
  if (pos == std::string::npos) {
    return false;
  }
  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos) {
    return false;
  }
  ++pos;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) {
    ++pos;
  }
  if (pos >= json.size() || json[pos] != '"') {
    return false;
  }
  ++pos;
  out.clear();
  while (pos < json.size()) {
    char ch = json[pos++];
    if (ch == '"') {
      return true;
    }
    if (ch != '\\') {
      out.push_back(ch);
      continue;
    }
    if (pos >= json.size()) {
      return false;
    }
    char esc = json[pos++];
    switch (esc) {
    case '"':
    case '\\':
    case '/':
      out.push_back(esc);
      break;
    case 'b':
      out.push_back('\b');
      break;
    case 'f':
      out.push_back('\f');
      break;
    case 'n':
      out.push_back('\n');
      break;
    case 'r':
      out.push_back('\r');
      break;
    case 't':
      out.push_back('\t');
      break;
    case 'u': {
      unsigned int codepoint = 0;
      if (!parse_hex4(json, pos, codepoint)) {
        return false;
      }
      pos += 4;
      if (codepoint >= 0xD800 && codepoint <= 0xDBFF && pos + 6 <= json.size() &&
          json[pos] == '\\' && json[pos + 1] == 'u') {
        unsigned int low = 0;
        if (parse_hex4(json, pos + 2, low) && low >= 0xDC00 && low <= 0xDFFF) {
          codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
          pos += 6;
        }
      }
      append_utf8(out, codepoint);
      break;
    }
    default:
      out.push_back(esc);
      break;
    }
  }
  return false;
}

std::wstring utf8_to_wide(const std::string &text) {
  if (text.empty()) {
    return L"";
  }
  int size = MultiByteToWideChar(CP_UTF8,
                                 MB_ERR_INVALID_CHARS,
                                 text.data(),
                                 static_cast<int>(std::min<size_t>(text.size(), INT_MAX)),
                                 nullptr,
                                 0);
  UINT codepage = CP_UTF8;
  DWORD flags = MB_ERR_INVALID_CHARS;
  if (size <= 0) {
    codepage = CP_ACP;
    flags = 0;
    size = MultiByteToWideChar(codepage,
                               flags,
                               text.data(),
                               static_cast<int>(std::min<size_t>(text.size(), INT_MAX)),
                               nullptr,
                               0);
  }
  if (size <= 0) {
    return L"";
  }
  std::wstring out(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(codepage,
                      flags,
                      text.data(),
                      static_cast<int>(std::min<size_t>(text.size(), INT_MAX)),
                      &out[0],
                      size);
  return out;
}

std::wstring trim(std::wstring value) {
  while (!value.empty() && std::iswspace(value.front())) {
    value.erase(value.begin());
  }
  while (!value.empty() && std::iswspace(value.back())) {
    value.pop_back();
  }
  return value;
}

std::wstring ellipsize(std::wstring value, size_t max_chars) {
  if (value.size() <= max_chars) {
    return value;
  }
  if (max_chars <= 3) {
    value.resize(max_chars);
    return value;
  }
  value.resize(max_chars - 3);
  value += L"...";
  return value;
}

std::vector<std::wstring> split_lines(const std::wstring &text) {
  std::vector<std::wstring> lines;
  std::wstringstream input(text);
  std::wstring line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == L'\r') {
      line.pop_back();
    }
    line = trim(line);
    if (!line.empty()) {
      lines.push_back(line);
    }
  }
  return lines;
}

std::vector<std::wstring> split_summary(const std::wstring &summary) {
  std::vector<std::wstring> parts;
  size_t start = 0;
  for (;;) {
    size_t next = summary.find(L"|", start);
    std::wstring part = trim(summary.substr(start, next == std::wstring::npos ? next : next - start));
    if (!part.empty()) {
      parts.push_back(part);
    }
    if (next == std::wstring::npos) {
      break;
    }
    start = next + 1;
  }
  if (parts.empty()) {
    parts.push_back(summary);
  }
  return parts;
}

std::wstring normalize_class(const std::wstring &klass, const std::wstring &summary) {
  std::wstring value = klass;
  std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
    return static_cast<wchar_t>(std::towlower(ch));
  });
  if (value == L"starting" || value == L"thinking" || value == L"working" || value == L"waiting" ||
      value == L"complete" || value == L"error" || value == L"idle" || value == L"stale") {
    return value;
  }
  std::wstring lower = summary;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t ch) {
    return static_cast<wchar_t>(std::towlower(ch));
  });
  if (lower.find(L"error") != std::wstring::npos) {
    return L"error";
  }
  if (lower.find(L"waiting") != std::wstring::npos) {
    return L"waiting";
  }
  if (lower.find(L"working") != std::wstring::npos) {
    return L"working";
  }
  if (lower.find(L"thinking") != std::wstring::npos) {
    return L"thinking";
  }
  if (lower.find(L"starting") != std::wstring::npos) {
    return L"starting";
  }
  if (lower.find(L"complete") != std::wstring::npos) {
    return L"complete";
  }
  return L"idle";
}

COLORREF color_for_class(const std::wstring &klass) {
  if (klass == L"error") {
    return RGB(238, 92, 92);
  }
  if (klass == L"waiting") {
    return RGB(229, 179, 72);
  }
  if (klass == L"working" || klass == L"thinking" || klass == L"starting") {
    return RGB(62, 188, 203);
  }
  if (klass == L"complete") {
    return RGB(91, 190, 128);
  }
  if (klass == L"stale") {
    return RGB(150, 157, 168);
  }
  return RGB(120, 132, 148);
}

FloatStatus query_status() {
  FloatStatus status;
  GetLocalTime(&status.updated);

  CommandResult command = run_capture(L"agent-waybar --provider all --max-sessions 6", 12000);
  if (!command.ok) {
    status.ok = false;
    status.state_class = command.timed_out ? L"waiting" : L"error";
    status.summary = command.timed_out ? L"cat-light status update timed out" : L"cat-light status unavailable";
    if (!command.error.empty()) {
      status.details.push_back(command.error);
    } else if (!command.output.empty()) {
      status.details.push_back(ellipsize(utf8_to_wide(command.output), 96));
    } else {
      status.details.push_back(L"Right-click to refresh or open dashboard.");
    }
    return status;
  }

  std::string summary;
  std::string tooltip;
  std::string klass;
  if (!json_string_field(command.output, "text", summary)) {
    summary = command.output;
  }
  json_string_field(command.output, "tooltip", tooltip);
  json_string_field(command.output, "class", klass);

  status.summary = trim(utf8_to_wide(summary));
  if (status.summary.empty()) {
    status.summary = L"No agent sessions found.";
  }
  status.state_class = normalize_class(utf8_to_wide(klass), status.summary);
  status.details = split_lines(utf8_to_wide(tooltip));
  if (status.details.empty()) {
    status.details.push_back(L"No active Codex or Claude sessions detected.");
  }
  status.ok = true;
  return status;
}

void request_refresh(HWND hwnd) {
  bool expected = false;
  if (!g_refreshing.compare_exchange_strong(expected, true)) {
    return;
  }
  std::thread([hwnd]() {
    FloatStatus next = query_status();
    {
      std::lock_guard<std::mutex> lock(g_status_mutex);
      g_status = std::move(next);
    }
    g_refreshing = false;
    if (!g_closing) {
      PostMessageW(hwnd, kStatusReadyMessage, 0, 0);
    }
  }).detach();
}

HFONT make_font(HWND hwnd, int points, int weight) {
  HDC dc = GetDC(hwnd);
  int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
  if (dc) {
    ReleaseDC(hwnd, dc);
  }
  return CreateFontW(-MulDiv(points, dpi, 72),
                     0,
                     0,
                     0,
                     weight,
                     FALSE,
                     FALSE,
                     FALSE,
                     DEFAULT_CHARSET,
                     OUT_DEFAULT_PRECIS,
                     CLIP_DEFAULT_PRECIS,
                     CLEARTYPE_QUALITY,
                     DEFAULT_PITCH | FF_DONTCARE,
                     L"Segoe UI");
}

void create_fonts(HWND hwnd) {
  g_fonts.title = make_font(hwnd, 13, FW_SEMIBOLD);
  g_fonts.row = make_font(hwnd, 10, FW_SEMIBOLD);
  g_fonts.detail = make_font(hwnd, 9, FW_NORMAL);
  g_fonts.tiny = make_font(hwnd, 8, FW_NORMAL);
}

void delete_fonts() {
  if (g_fonts.title) {
    DeleteObject(g_fonts.title);
    g_fonts.title = nullptr;
  }
  if (g_fonts.row) {
    DeleteObject(g_fonts.row);
    g_fonts.row = nullptr;
  }
  if (g_fonts.detail) {
    DeleteObject(g_fonts.detail);
    g_fonts.detail = nullptr;
  }
  if (g_fonts.tiny) {
    DeleteObject(g_fonts.tiny);
    g_fonts.tiny = nullptr;
  }
}

void draw_text_line(HDC dc, HFONT font, COLORREF color, const std::wstring &text, RECT rect, UINT extra_format = 0) {
  HFONT old_font = static_cast<HFONT>(SelectObject(dc, font));
  SetTextColor(dc, color);
  SetBkMode(dc, TRANSPARENT);
  DrawTextW(dc,
            text.c_str(),
            static_cast<int>(text.size()),
            &rect,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | extra_format);
  SelectObject(dc, old_font);
}

void fill_round_rect(HDC dc, const RECT &rect, COLORREF fill, COLORREF border) {
  HBRUSH brush = CreateSolidBrush(fill);
  HPEN pen = CreatePen(PS_SOLID, 1, border);
  HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(dc, brush));
  HPEN old_pen = static_cast<HPEN>(SelectObject(dc, pen));
  RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 8, 8);
  SelectObject(dc, old_pen);
  SelectObject(dc, old_brush);
  DeleteObject(pen);
  DeleteObject(brush);
}

void fill_rect(HDC dc, const RECT &rect, COLORREF color) {
  HBRUSH brush = CreateSolidBrush(color);
  FillRect(dc, &rect, brush);
  DeleteObject(brush);
}

void draw_dot(HDC dc, int x, int y, int size, COLORREF color) {
  HBRUSH brush = CreateSolidBrush(color);
  HPEN pen = CreatePen(PS_SOLID, 1, color);
  HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(dc, brush));
  HPEN old_pen = static_cast<HPEN>(SelectObject(dc, pen));
  Ellipse(dc, x, y, x + size, y + size);
  SelectObject(dc, old_pen);
  SelectObject(dc, old_brush);
  DeleteObject(pen);
  DeleteObject(brush);
}

void draw_status(HDC dc, const RECT &client, const FloatStatus &status) {
  const COLORREF bg = RGB(22, 24, 27);
  const COLORREF border = RGB(57, 62, 70);
  const COLORREF muted = RGB(155, 164, 176);
  const COLORREF text = RGB(238, 241, 245);
  const COLORREF detail = RGB(193, 201, 211);
  const COLORREF panel = RGB(31, 34, 38);
  const COLORREF line = RGB(68, 74, 84);
  const COLORREF accent = color_for_class(status.state_class);

  fill_round_rect(dc, client, bg, border);

  RECT accent_rect{client.left + 10, client.top + 12, client.left + 14, client.bottom - 12};
  fill_rect(dc, accent_rect, accent);

  RECT title_rect{client.left + 24, client.top + 10, client.right - 96, client.top + 34};
  draw_text_line(dc, g_fonts.title, text, L"cat-light", title_rect);

  wchar_t time_text[32]{};
  swprintf_s(time_text, L"%02u:%02u", status.updated.wHour, status.updated.wMinute);
  RECT time_rect{client.right - 82, client.top + 10, client.right - 18, client.top + 34};
  draw_text_line(dc, g_fonts.tiny, muted, time_text, time_rect, DT_RIGHT);

  draw_dot(dc, client.left + 24, client.top + 43, 9, accent);
  RECT class_rect{client.left + 39, client.top + 37, client.right - 18, client.top + 58};
  draw_text_line(dc, g_fonts.tiny, muted, status.state_class, class_rect);

  std::vector<std::wstring> summary_rows = split_summary(status.summary);
  int y = 63;
  for (size_t i = 0; i < summary_rows.size() && i < 2; ++i) {
    RECT row_bg{client.left + 22, y, client.right - 16, y + 27};
    fill_round_rect(dc, row_bg, panel, RGB(40, 44, 50));
    draw_dot(dc, row_bg.left + 9, row_bg.top + 9, 7, accent);
    RECT row_text{row_bg.left + 23, row_bg.top + 2, row_bg.right - 8, row_bg.bottom - 1};
    draw_text_line(dc, g_fonts.row, text, ellipsize(summary_rows[i], 48), row_text);
    y += 32;
  }

  RECT separator{client.left + 24, y + 2, client.right - 18, y + 3};
  fill_rect(dc, separator, line);
  y += 9;

  int drawn = 0;
  for (const std::wstring &raw : status.details) {
    if (drawn >= 3 || y > client.bottom - 18) {
      break;
    }
    std::wstring value = ellipsize(trim(raw), 64);
    RECT detail_rect{client.left + 24, y, client.right - 18, y + 19};
    draw_text_line(dc, g_fonts.detail, drawn == 0 ? detail : muted, value, detail_rect);
    y += 19;
    ++drawn;
  }
}

void paint(HWND hwnd) {
  PAINTSTRUCT ps{};
  HDC dc = BeginPaint(hwnd, &ps);
  RECT client{};
  GetClientRect(hwnd, &client);

  HDC mem_dc = CreateCompatibleDC(dc);
  HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right - client.left, client.bottom - client.top);
  HBITMAP old_bitmap = static_cast<HBITMAP>(SelectObject(mem_dc, bitmap));
  SetBkMode(mem_dc, TRANSPARENT);

  FloatStatus snapshot;
  {
    std::lock_guard<std::mutex> lock(g_status_mutex);
    snapshot = g_status;
  }
  draw_status(mem_dc, client, snapshot);
  BitBlt(dc, 0, 0, client.right - client.left, client.bottom - client.top, mem_dc, 0, 0, SRCCOPY);

  SelectObject(mem_dc, old_bitmap);
  DeleteObject(bitmap);
  DeleteDC(mem_dc);
  EndPaint(hwnd, &ps);
}

void show_menu(HWND hwnd) {
  POINT point{};
  GetCursorPos(&point);
  HMENU menu = CreatePopupMenu();
  if (!menu) {
    return;
  }
  AppendMenuW(menu, MF_STRING, kMenuOpen, L"Open dashboard");
  AppendMenuW(menu, MF_STRING, kMenuSync, L"Sync history");
  AppendMenuW(menu, MF_STRING, kMenuHookStatus, L"Hook status");
  AppendMenuW(menu, MF_STRING, kMenuRefresh, L"Refresh now");
  AppendMenuW(menu, MF_STRING, kMenuResetPosition, L"Reset position");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kMenuQuit, L"Quit");
  SetForegroundWindow(hwnd);
  TrackPopupMenu(menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, point.x, point.y, 0, hwnd, nullptr);
  DestroyMenu(menu);
}

void clamp_to_work_area(int &x, int &y) {
  POINT point{x, y};
  HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  RECT work{};
  if (monitor && GetMonitorInfoW(monitor, &info)) {
    work = info.rcWork;
  } else if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
    work = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
  }
  const int left = static_cast<int>(work.left);
  const int top = static_cast<int>(work.top);
  const int right = static_cast<int>(work.right);
  const int bottom = static_cast<int>(work.bottom);
  x = std::max(left, std::min(x, right - kWindowWidth));
  y = std::max(top, std::min(y, bottom - kWindowHeight));
}

void position_default(HWND hwnd) {
  RECT work{};
  if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
    work = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
  }
  int x = work.right - kWindowWidth - 22;
  int y = work.bottom - kWindowHeight - 34;
  clamp_to_work_area(x, y);
  SetWindowPos(hwnd, HWND_TOPMOST, x, y, kWindowWidth, kWindowHeight, SWP_NOACTIVATE);
}

bool load_position(HWND hwnd) {
  const std::wstring path = float_config_path();
  int x = GetPrivateProfileIntW(L"window", L"x", kMissingPosition, path.c_str());
  int y = GetPrivateProfileIntW(L"window", L"y", kMissingPosition, path.c_str());
  if (x == kMissingPosition || y == kMissingPosition) {
    return false;
  }
  clamp_to_work_area(x, y);
  SetWindowPos(hwnd, HWND_TOPMOST, x, y, kWindowWidth, kWindowHeight, SWP_NOACTIVATE);
  return true;
}

void save_position(HWND hwnd) {
  RECT rect{};
  if (!GetWindowRect(hwnd, &rect)) {
    return;
  }
  const std::wstring path = float_config_path();
  wchar_t x[32]{};
  wchar_t y[32]{};
  swprintf_s(x, L"%ld", rect.left);
  swprintf_s(y, L"%ld", rect.top);
  WritePrivateProfileStringW(L"window", L"x", x, path.c_str());
  WritePrivateProfileStringW(L"window", L"y", y, path.c_str());
}

void reset_position(HWND hwnd) {
  const std::wstring path = float_config_path();
  WritePrivateProfileStringW(L"window", L"x", nullptr, path.c_str());
  WritePrivateProfileStringW(L"window", L"y", nullptr, path.c_str());
  position_default(hwnd);
  save_position(hwnd);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
  case WM_CREATE:
    create_fonts(hwnd);
    SetLayeredWindowAttributes(hwnd, 0, 250, LWA_ALPHA);
    start_server();
    SetTimer(hwnd, kRefreshTimer, 5000, nullptr);
    SetTimer(hwnd, kServerTimer, 15000, nullptr);
    request_refresh(hwnd);
    return 0;
  case WM_TIMER:
    if (wparam == kRefreshTimer) {
      request_refresh(hwnd);
    } else if (wparam == kServerTimer && !process_alive(g_server)) {
      start_server();
    }
    return 0;
  case kStatusReadyMessage:
    InvalidateRect(hwnd, nullptr, FALSE);
    return 0;
  case WM_PAINT:
    paint(hwnd);
    return 0;
  case WM_NCHITTEST: {
    LRESULT hit = DefWindowProcW(hwnd, msg, wparam, lparam);
    if (hit == HTCLIENT) {
      return HTCAPTION;
    }
    return hit;
  }
  case WM_LBUTTONDBLCLK:
    open_dashboard();
    return 0;
  case WM_EXITSIZEMOVE:
    save_position(hwnd);
    return 0;
  case WM_RBUTTONUP:
  case WM_CONTEXTMENU:
    show_menu(hwnd);
    return 0;
  case WM_COMMAND:
    switch (LOWORD(wparam)) {
    case kMenuOpen:
      open_dashboard();
      return 0;
    case kMenuSync:
      run_hidden(L"sync");
      request_refresh(hwnd);
      return 0;
    case kMenuHookStatus:
      run_visible_console(L"hook-status --provider all");
      return 0;
    case kMenuRefresh:
      request_refresh(hwnd);
      return 0;
    case kMenuResetPosition:
      reset_position(hwnd);
      return 0;
    case kMenuQuit:
      DestroyWindow(hwnd);
      return 0;
    default:
      break;
    }
    break;
  case WM_DESTROY:
    g_closing = true;
    save_position(hwnd);
    KillTimer(hwnd, kRefreshTimer);
    KillTimer(hwnd, kServerTimer);
    stop_server();
    delete_fonts();
    PostQuitMessage(0);
    return 0;
  default:
    break;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_cmd) {
  const std::wstring class_name = L"CatLightFloatWindow";
  g_cat_light = module_dir() + L"\\cat-light.exe";
  GetLocalTime(&g_status.updated);

  WNDCLASSW wc{};
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = class_name.c_str();
  RegisterClassW(&wc);

  HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                              class_name.c_str(),
                              L"cat-light float",
                              WS_POPUP,
                              CW_USEDEFAULT,
                              CW_USEDEFAULT,
                              kWindowWidth,
                              kWindowHeight,
                              nullptr,
                              nullptr,
                              instance,
                              nullptr);
  if (!hwnd) {
    return 1;
  }

  if (!load_position(hwnd)) {
    position_default(hwnd);
  }
  ShowWindow(hwnd, show_cmd == SW_HIDE ? SW_SHOWNOACTIVATE : SW_SHOWNOACTIVATE);
  UpdateWindow(hwnd);

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}
