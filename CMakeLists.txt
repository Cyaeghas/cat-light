cmake_minimum_required(VERSION 3.20)

project(cat_light VERSION 0.1.0 LANGUAGES CXX)

option(CAT_LIGHT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(CAT_LIGHT_ENABLE_SQLITE "Build optional SQLite history backend" OFF)

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
  find_package(SQLite3 REQUIRED)
  target_sources(cat-light PRIVATE src/history_store_sqlite.cpp)
  target_link_libraries(cat-light PRIVATE SQLite::SQLite3)
  target_compile_definitions(cat-light PRIVATE CAT_LIGHT_HAS_SQLITE=1)
endif()

install(TARGETS cat-light RUNTIME DESTINATION bin)
