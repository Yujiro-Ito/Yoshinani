// 1-C トリガー確定 — 判定表のユニットテスト（最初の実テスト）。
#include <doctest/doctest.h>

#include "domain/TriggerPolicy.h"

using namespace yoshinani::core::domain;

TEST_CASE("Character は常に Append") {
    CHECK(Decide(KeyKind::Character, true)  == InputAction::Append);
    CHECK(Decide(KeyKind::Character, false) == InputAction::Append);
}

TEST_CASE("Trigger(Tab): 非空なら Commit / 空なら PassThrough") {
    CHECK(Decide(KeyKind::Trigger, false) == InputAction::Commit);
    CHECK(Decide(KeyKind::Trigger, true)  == InputAction::PassThrough);
}

TEST_CASE("Space(区切り): 非空なら Append / 空なら PassThrough") {
    CHECK(Decide(KeyKind::Space, false) == InputAction::Append);
    CHECK(Decide(KeyKind::Space, true)  == InputAction::PassThrough);
}

TEST_CASE("Backspace: 非空なら DeleteLast / 空なら PassThrough") {
    CHECK(Decide(KeyKind::Backspace, false) == InputAction::DeleteLast);
    CHECK(Decide(KeyKind::Backspace, true)  == InputAction::PassThrough);
}

TEST_CASE("Esc: 非空なら Cancel / 空なら PassThrough") {
    CHECK(Decide(KeyKind::Escape, false) == InputAction::Cancel);
    CHECK(Decide(KeyKind::Escape, true)  == InputAction::PassThrough);
}

TEST_CASE("Other は常に PassThrough") {
    CHECK(Decide(KeyKind::Other, true)  == InputAction::PassThrough);
    CHECK(Decide(KeyKind::Other, false) == InputAction::PassThrough);
}
