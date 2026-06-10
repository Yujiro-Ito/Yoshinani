// 1-D 入力モード — ユニットテスト。
#include <doctest/doctest.h>

#include "application/InputMode.h"

using yoshinani::core::application::InputMode;
using yoshinani::core::application::InputModeState;

TEST_CASE("既定は変換モード") {
    InputModeState s;
    CHECK(s.Get() == InputMode::Conversion);
    CHECK_FALSE(s.IsDirect());
}

TEST_CASE("Toggle で 変換⇄直接 を往復する") {
    InputModeState s;
    CHECK(s.Toggle() == InputMode::Direct);
    CHECK(s.IsDirect());
    CHECK(s.Toggle() == InputMode::Conversion);
    CHECK_FALSE(s.IsDirect());
}

TEST_CASE("Set で外部（コンパートメント）からの状態を反映できる") {
    InputModeState s;
    s.Set(InputMode::Direct);
    CHECK(s.IsDirect());
    s.Set(InputMode::Conversion);
    CHECK_FALSE(s.IsDirect());
}
