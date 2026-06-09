// 1-B preedit — InputSession のユニットテスト。
#include <doctest/doctest.h>

#include "application/InputSession.h"

using yoshinani::core::application::InputSession;

TEST_CASE("初期状態は空") {
    InputSession s;
    CHECK(s.Empty());
    CHECK(s.Preedit().empty());
}

TEST_CASE("Append で溜まり、順序が保たれる") {
    InputSession s;
    s.AppendChar(u'k');
    s.AppendChar(u'a');
    CHECK_FALSE(s.Empty());
    CHECK(s.Preedit() == std::u16string(u"ka"));
}

TEST_CASE("Backspace は末尾を1文字削る。空への余分な Backspace は無害") {
    InputSession s;
    s.AppendChar(u'a');
    s.AppendChar(u'b');
    s.Backspace();
    CHECK(s.Preedit() == std::u16string(u"a"));
    s.Backspace();
    CHECK(s.Empty());
    s.Backspace();  // 空でも落ちない
    CHECK(s.Empty());
}

TEST_CASE("Clear で空になる") {
    InputSession s;
    s.AppendChar(u'x');
    s.Clear();
    CHECK(s.Empty());
}
