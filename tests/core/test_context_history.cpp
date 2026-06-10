// 継続モード（文脈チェイン）— ContextHistory のユニットテスト。
#include <doctest/doctest.h>

#include "application/ContextHistory.h"

using yoshinani::core::application::ContextHistory;

TEST_CASE("初期状態は空・Tail も空") {
    ContextHistory h;
    CHECK(h.Empty());
    CHECK(h.Tail().empty());
}

TEST_CASE("Push した確定文が古い順に空白連結される") {
    ContextHistory h;
    h.Push(u"今日は天気がいい");
    h.Push(u"散歩に行こう");
    CHECK(h.Tail() == std::u16string(u"今日は天気がいい 散歩に行こう"));
}

TEST_CASE("空文字の Push は無視") {
    ContextHistory h;
    h.Push(u"");
    CHECK(h.Empty());
}

TEST_CASE("maxEntries を超えると古いものから捨てる") {
    ContextHistory h(2, 240);
    h.Push(u"a");
    h.Push(u"b");
    h.Push(u"c");
    CHECK(h.Tail() == std::u16string(u"b c"));
}

TEST_CASE("maxChars を超えると先頭（古い側）から切り詰める") {
    ContextHistory h(3, 5);
    h.Push(u"12345");
    h.Push(u"abc");
    // 連結 "12345 abc"(9文字) → 末尾5文字 "5 abc"
    CHECK(h.Tail() == std::u16string(u"5 abc"));
}

TEST_CASE("Clear で空に戻る") {
    ContextHistory h;
    h.Push(u"x");
    h.Clear();
    CHECK(h.Empty());
    CHECK(h.Tail().empty());
}
