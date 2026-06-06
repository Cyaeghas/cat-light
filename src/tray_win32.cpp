#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace {

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kTimerId = 1;
constexpr int kMenuOpen = 1001;
constexpr int kMenuSync = 1002;
constexpr int kMenuHookStatus = 1003;
constexpr int kMenuQuit = 1004;

NOTIFYICONDATAW g_nid{};
PROCESS_INFORMATION g_server{};
std::wstring g_cat_light;

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

void update_tip(const wchar_t *text) {
  wcsncpy_s(g_nid.szTip, text, _TRUNCATE);
  Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

bool process_alive(const PROCESS_INFORMATION &process) {
  if (!process.hProcess) {
    return false;
  }
  DWORD code = 0;
  return GetExitCodeProcess(process.hProcess, &code) && code == STILL_ACTIVE;
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
  update_tip(ok ? L"cat-light: server running" : L"cat-light: server failed to start");
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
  std::wstring command = L"/k " + quote(g_cat_light) + L" " + args;
  ShellExecuteW(nullptr, L"open", L"cmd.exe", command.c_str(), module_dir().c_str(), SW_SHOWNORMAL);
}

void open_dashboard() {
  start_server();
  ShellExecuteW(nullptr, L"open", L"http://127.0.0.1:8750", nullptr, nullptr, SW_SHOWNORMAL);
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
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kMenuQuit, L"Quit");
  SetForegroundWindow(hwnd);
  TrackPopupMenu(menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, point.x, point.y, 0, hwnd, nullptr);
  DestroyMenu(menu);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
  case WM_CREATE:
    SetTimer(hwnd, kTimerId, 5000, nullptr);
    start_server();
    return 0;
  case WM_TIMER:
    if (!process_alive(g_server)) {
      start_server();
    }
    return 0;
  case kTrayMessage:
    if (lparam == WM_LBUTTONUP) {
      open_dashboard();
    } else if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU) {
      show_menu(hwnd);
    }
    return 0;
  case WM_COMMAND:
    switch (LOWORD(wparam)) {
    case kMenuOpen:
      open_dashboard();
      return 0;
    case kMenuSync:
      run_hidden(L"sync");
      update_tip(L"cat-light: sync requested");
      return 0;
    case kMenuHookStatus:
      run_visible_console(L"hook-status --provider all");
      return 0;
    case kMenuQuit:
      DestroyWindow(hwnd);
      return 0;
    default:
      break;
    }
    break;
  case WM_DESTROY:
    KillTimer(hwnd, kTimerId);
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    stop_server();
    PostQuitMessage(0);
    return 0;
  default:
    break;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  const std::wstring class_name = L"CatLightTrayWindow";
  g_cat_light = module_dir() + L"\\cat-light.exe";

  WNDCLASSW wc{};
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = instance;
  wc.lpszClassName = class_name.c_str();
  RegisterClassW(&wc);

  HWND hwnd = CreateWindowExW(0,
                              class_name.c_str(),
                              L"cat-light tray",
                              0,
                              0,
                              0,
                              0,
                              0,
                              HWND_MESSAGE,
                              nullptr,
                              instance,
                              nullptr);
  if (!hwnd) {
    return 1;
  }

  g_nid.cbSize = sizeof(g_nid);
  g_nid.hWnd = hwnd;
  g_nid.uID = 1;
  g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  g_nid.uCallbackMessage = kTrayMessage;
  g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wcsncpy_s(g_nid.szTip, L"cat-light: starting", _TRUNCATE);
  Shell_NotifyIconW(NIM_ADD, &g_nid);

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}
