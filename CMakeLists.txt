cmake_minimum_required(VERSION 3.20)

project(cat_light VERSION 0.1.0 LANGUAGES C CXX)

option(CAT_LIGHT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(CAT_LIGHT_ENABLE_SQLITE "Build optional SQLite history backend" OFF)
option(CAT_LIGHT_USE_BUNDLED_SQLITE "Use vendored SQLite amalgamation when SQLite backend is enabled" ON)
option(CAT_LIGHT_ENABLE_TRAY "Build optional Windows tray launcher" ON)
option(CAT_LIGHT_ENABLE_FLOAT "Build optional Windows floating monitor" ON)

add_executable(cat-light
  src/main.cpp
  src/agent.cpp
  src/agent_scan.cpp
  src/app.cpp
  src/hooks.cpp
  src/history_store.cpp
  src/history_store_jsonl.cpp
  src/http_client.cpp
  src/json.cpp
  src/provider_claude.cpp
  src/provider_codex.cpp
  src/render.cpp
  src/server.cpp
  src/sync.cpp
  src/util.cpp
)

target_compile_features(cat-light PRIVATE cxx_std_17)
target_include_directories(cat-light PRIVATE src)
set_property(TARGET cat-light PROPERTY VS_GLOBAL_VcpkgEnabled false)

if(MSVC)
  target_compile_definitions(cat-light PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
  target_compile_options(cat-light PRIVATE /W4 /permissive- /utf-8)
  if(CAT_LIGHT_WARNINGS_AS_ERRORS)
    target_compile_options(cat-light PRIVATE /WX)
  endif()
else()
  target_compile_options(cat-light PRIVATE -Wall -Wextra -Wpedantic)
  if(CAT_LIGHT_WARNINGS_AS_ERRORS)
    target_compile_options(cat-light PRIVATE -Werror)
  endif()
endif()

if(WIN32)
  target_link_libraries(cat-light PRIVATE ws2_32)
endif()

if(CAT_LIGHT_ENABLE_SQLITE)
  target_sources(cat-light PRIVATE src/history_store_sqlite.cpp)
  if(CAT_LIGHT_USE_BUNDLED_SQLITE)
    add_library(cat-light-sqlite STATIC vendor/sqlite/sqlite3.c)
    target_include_directories(cat-light-sqlite PUBLIC vendor/sqlite)
    target_compile_definitions(cat-light-sqlite PRIVATE
      SQLITE_OMIT_LOAD_EXTENSION
      SQLITE_THREADSAFE=1
      SQLITE_DEFAULT_MEMSTATUS=0
    )
    if(MSVC)
      target_compile_definitions(cat-light-sqlite PRIVATE _CRT_SECURE_NO_WARNINGS)
      target_compile_options(cat-light-sqlite PRIVATE /W0 /utf-8)
    else()
      target_compile_options(cat-light-sqlite PRIVATE -w)
    endif()
    target_link_libraries(cat-light PRIVATE cat-light-sqlite)
    target_compile_definitions(cat-light PRIVATE CAT_LIGHT_BUNDLED_SQLITE=1)
  else()
    find_package(SQLite3 REQUIRED)
    target_link_libraries(cat-light PRIVATE SQLite::SQLite3)
  endif()
  target_compile_definitions(cat-light PRIVATE CAT_LIGHT_HAS_SQLITE=1)
endif()

if(WIN32 AND CAT_LIGHT_ENABLE_TRAY)
  add_executable(cat-light-tray WIN32 src/tray_win32.cpp)
  target_compile_features(cat-light-tray PRIVATE cxx_std_17)
  set_property(TARGET cat-light-tray PROPERTY VS_GLOBAL_VcpkgEnabled false)
  if(MSVC)
    target_compile_definitions(cat-light-tray PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX UNICODE _UNICODE)
    target_compile_options(cat-light-tray PRIVATE /W4 /permissive- /utf-8)
    if(CAT_LIGHT_WARNINGS_AS_ERRORS)
      target_compile_options(cat-light-tray PRIVATE /WX)
    endif()
  else()
    target_compile_definitions(cat-light-tray PRIVATE UNICODE _UNICODE)
    target_compile_options(cat-light-tray PRIVATE -Wall -Wextra -Wpedantic)
    if(CAT_LIGHT_WARNINGS_AS_ERRORS)
      target_compile_options(cat-light-tray PRIVATE -Werror)
    endif()
  endif()
  target_link_libraries(cat-light-tray PRIVATE shell32 user32)
endif()

if(WIN32 AND CAT_LIGHT_ENABLE_FLOAT)
  add_executable(cat-light-float WIN32 src/float_win32.cpp)
  target_compile_features(cat-light-float PRIVATE cxx_std_17)
  set_property(TARGET cat-light-float PROPERTY VS_GLOBAL_VcpkgEnabled false)
  if(MSVC)
    target_compile_definitions(cat-light-float PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX UNICODE _UNICODE)
    target_compile_options(cat-light-float PRIVATE /W4 /permissive- /utf-8)
    if(CAT_LIGHT_WARNINGS_AS_ERRORS)
      target_compile_options(cat-light-float PRIVATE /WX)
    endif()
  else()
    target_compile_definitions(cat-light-float PRIVATE UNICODE _UNICODE)
    target_compile_options(cat-light-float PRIVATE -Wall -Wextra -Wpedantic)
    if(CAT_LIGHT_WARNINGS_AS_ERRORS)
      target_compile_options(cat-light-float PRIVATE -Werror)
    endif()
  endif()
  target_link_libraries(cat-light-float PRIVATE shell32 user32 gdi32 advapi32)
endif()

install(TARGETS cat-light RUNTIME DESTINATION bin)
if(TARGET cat-light-tray)
  install(TARGETS cat-light-tray RUNTIME DESTINATION bin)
endif()
if(TARGET cat-light-float)
  install(TARGETS cat-light-float RUNTIME DESTINATION bin)
endif()
