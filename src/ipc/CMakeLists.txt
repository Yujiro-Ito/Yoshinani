# yoshinani.ipc — infra（名前付きパイプ・クライアント。実装は 3-C）
# 現状はターゲット構成だけ用意したプレースホルダ。
add_library(yoshinani.ipc STATIC
  Placeholder.cpp
)
target_include_directories(yoshinani.ipc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(yoshinani.ipc PUBLIC yoshinani.core)
