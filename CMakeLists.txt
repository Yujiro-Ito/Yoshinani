# よしなに (Yoshinani) — ルート CMake
# 0-A レイヤ分割を CMake ターゲットに 1:1 で落とす。
#   yoshinani.core  : domain + application（OS非依存・テスト対象）
#   yoshinani.ipc   : infra（名前付きパイプ。3-C で実装）
#   yoshinani.tsf   : infra（→ yoshinani.dll。TSF/COM。Windows のみ）
#   yoshinanid      : zenz デーモン（llama.cpp。3-B。既定 OFF）
#   yoshinani.core.tests : doctest による core ユニットテスト
cmake_minimum_required(VERSION 3.20)
project(yoshinani LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/utf-8 /W4 /permissive-)
endif()

option(YOSHINANI_BUILD_TSF    "Build the TSF DLL (Windows only)"        ON)
option(YOSHINANI_BUILD_TRAY   "Build the tray EXE (Windows only)"       ON)
option(YOSHINANI_BUILD_DAEMON "Build the zenz daemon (needs llama.cpp)" OFF)
option(YOSHINANI_BUILD_TESTS  "Build core unit tests"                   ON)

# nlohmann/json（設定ファイル JSON 用）。単一ヘッダだけ取得し、
# nlohmann_json 自身の CMake プロジェクトは使わない（CMake 4.x との相性問題を回避）。
set(_json_dir ${CMAKE_BINARY_DIR}/_deps/nlohmann-json)
set(_json_hpp ${_json_dir}/nlohmann/json.hpp)
if(NOT EXISTS ${_json_hpp})
  message(STATUS "Downloading nlohmann/json single header ...")
  file(DOWNLOAD
    https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
    ${_json_hpp}
    TLS_VERIFY ON
    STATUS _json_dl_status)
  list(GET _json_dl_status 0 _json_dl_code)
  if(NOT _json_dl_code EQUAL 0)
    message(FATAL_ERROR "nlohmann/json のダウンロードに失敗: ${_json_dl_status}")
  endif()
endif()
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${_json_dir})

add_subdirectory(src/core)
add_subdirectory(src/ipc)

if(YOSHINANI_BUILD_TSF AND WIN32)
  add_subdirectory(src/tsf)
endif()

if(YOSHINANI_BUILD_TRAY AND WIN32)
  add_subdirectory(src/tray)
endif()

if(YOSHINANI_BUILD_DAEMON)
  add_subdirectory(src/daemon)
endif()

if(YOSHINANI_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests/core)
endif()
