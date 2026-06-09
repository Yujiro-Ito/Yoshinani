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
option(YOSHINANI_BUILD_DAEMON "Build the zenz daemon (needs llama.cpp)" OFF)
option(YOSHINANI_BUILD_TESTS  "Build core unit tests"                   ON)

add_subdirectory(src/core)
add_subdirectory(src/ipc)

if(YOSHINANI_BUILD_TSF AND WIN32)
  add_subdirectory(src/tsf)
endif()

if(YOSHINANI_BUILD_DAEMON)
  add_subdirectory(src/daemon)
endif()

if(YOSHINANI_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests/core)
endif()
