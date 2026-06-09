// 設定 JSON パーサのユニットテスト。
#include <doctest/doctest.h>

#include "application/Settings.h"

using yoshinani::core::application::ParseSettings;

TEST_CASE("既定（空文字）は triggerKeys = [Tab]") {
    auto s = ParseSettings("");
    REQUIRE(s.triggerKeys.size() == 1);
    CHECK(s.triggerKeys[0] == "Tab");
}

TEST_CASE("triggerKeys を JSON から読む") {
    auto s = ParseSettings(R"({"triggerKeys":["Period","Comma"]})");
    REQUIRE(s.triggerKeys.size() == 2);
    CHECK(s.triggerKeys[0] == "Period");
    CHECK(s.triggerKeys[1] == "Comma");
}

TEST_CASE("不正JSONは既定にフォールバック（例外を投げない）") {
    auto s = ParseSettings("{ not valid json ");
    REQUIRE(s.triggerKeys.size() == 1);
    CHECK(s.triggerKeys[0] == "Tab");
}

TEST_CASE("triggerKeys が空配列なら既定を維持") {
    auto s = ParseSettings(R"({"triggerKeys":[]})");
    REQUIRE(s.triggerKeys.size() == 1);
    CHECK(s.triggerKeys[0] == "Tab");
}
