# yoshinani.core.tests — doctest による core ユニットテスト（0-C）
# doctest はヘッダオンリー。SOURCE_SUBDIR を実在しないパスにして
# doctest 自身の CMake プロジェクト（古い cmake_minimum_required を持つ）を構成させず、
# 取得したヘッダだけを INTERFACE ターゲットとして使う（CMake バージョン非依存で堅牢）。
include(FetchContent)
FetchContent_Declare(
  doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG        v2.4.11
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  __header_only__   # 実在しない → add_subdirectory されない
)
FetchContent_MakeAvailable(doctest)

add_library(doctest_header INTERFACE)
target_include_directories(doctest_header INTERFACE ${doctest_SOURCE_DIR})

add_executable(yoshinani.core.tests
  test_main.cpp
  test_trigger_policy.cpp
  test_input_session.cpp
  test_settings.cpp
  test_conversion_queue.cpp
  test_context_history.cpp
  test_input_mode.cpp
)
target_link_libraries(yoshinani.core.tests PRIVATE yoshinani.core doctest_header)

add_test(NAME core COMMAND yoshinani.core.tests)
