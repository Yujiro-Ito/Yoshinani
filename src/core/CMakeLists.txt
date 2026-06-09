# yoshinani.core — domain + application（0-A）
# 重要: OS 非依存を保つ。Windows ライブラリをリンクしない / TSF ヘッダを通さない。
add_library(yoshinani.core STATIC
  domain/TriggerPolicy.cpp
  application/Settings.cpp
)

# 公開 include ルートを src/core にする → #include "domain/TriggerPolicy.h"
target_include_directories(yoshinani.core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(yoshinani.core PUBLIC cxx_std_20)

# JSON パースは Settings.cpp 内に閉じる（公開ヘッダには漏らさない）ので PRIVATE。
# nlohmann/json は純C++ヘッダのみ → core の OS 非依存は保たれる。
target_link_libraries(yoshinani.core PRIVATE nlohmann_json)
