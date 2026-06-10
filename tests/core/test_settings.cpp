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

TEST_CASE("modeToggleKeys: 既定は Kanji（半角/全角）・JSON から上書き可・空配列は既定維持") {
    auto d = ParseSettings("");
    REQUIRE(d.modeToggleKeys.size() == 1);
    CHECK(d.modeToggleKeys[0] == "Kanji");

    auto s = ParseSettings(R"({"modeToggleKeys":["Tab"]})");
    REQUIRE(s.modeToggleKeys.size() == 1);
    CHECK(s.modeToggleKeys[0] == "Tab");

    auto e = ParseSettings(R"({"modeToggleKeys":[]})");
    REQUIRE(e.modeToggleKeys.size() == 1);
    CHECK(e.modeToggleKeys[0] == "Kanji");
}

TEST_CASE("converter の既定は openai / model 空(バックエンド既定) / low") {
    auto s = ParseSettings("");
    CHECK(s.converter.backend == "openai");
    CHECK(s.converter.model.empty());
    CHECK(s.converter.reasoningEffort == "low");
}

TEST_CASE("converter を JSON から読む") {
    auto s = ParseSettings(
        R"({"converter":{"backend":"ollama","model":"gemma4:e2b-it-qat","reasoningEffort":"low"}})");
    CHECK(s.converter.backend == "ollama");
    CHECK(s.converter.model == "gemma4:e2b-it-qat");
    CHECK(s.converter.reasoningEffort == "low");
}

TEST_CASE("converter の部分指定・空文字は既定を保つ（トレイUIの部分書き換えに安全）") {
    auto s = ParseSettings(R"({"converter":{"backend":"","model":"gpt-5.4-nano"}})");
    CHECK(s.converter.backend == "openai");       // 空文字 → 既定維持
    CHECK(s.converter.model == "gpt-5.4-nano");
    CHECK(s.converter.reasoningEffort == "low");  // 未指定 → 既定維持
}
