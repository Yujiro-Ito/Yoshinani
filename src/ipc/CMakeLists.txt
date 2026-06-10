# yoshinani.ipc — infra（変換器の実装。v1 は Ollama/HTTP、将来 名前付きパイプ 3-C）
# WinHTTP を使うため Windows 専用。core は OS 非依存のまま、Windows 依存は本層に閉じ込める。
add_library(yoshinani.ipc STATIC
  Placeholder.cpp
  HttpJson.cpp
  OllamaKanaKanjiConverter.cpp
  OpenAiKanaKanjiConverter.cpp
)
target_include_directories(yoshinani.ipc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(yoshinani.ipc PUBLIC yoshinani.core)
# JSON(単一ヘッダ) と WinHTTP は ipc 内部に閉じる（公開ヘッダには漏らさない）→ PRIVATE。
target_link_libraries(yoshinani.ipc PRIVATE nlohmann_json winhttp)
