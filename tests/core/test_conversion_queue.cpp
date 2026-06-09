// 3-A/3-D 変換ポート & 変換キューのユニットテスト（モック変換器使用）。
#include <doctest/doctest.h>

#include <functional>
#include <string>
#include <string_view>

#include "application/ConversionQueue.h"
#include "domain/ports/IKanaKanjiConverter.h"

using yoshinani::core::application::ConversionQueue;
using yoshinani::core::application::ConversionRequest;
using yoshinani::core::application::ConvState;
using yoshinani::core::domain::ConversionResult;
using yoshinani::core::domain::IKanaKanjiConverter;
using yoshinani::core::domain::RequestId;

namespace {

// input をそのまま「<input>。」に変換するだけの同期モック（3-A 実装の代役）。
class MockConverter : public IKanaKanjiConverter {
public:
    void Convert(RequestId id, std::u16string_view input,
                 std::function<void(RequestId, ConversionResult)> onDone) override {
        ++calls;
        onDone(id, ConversionResult{std::u16string(input) + u"。", true});
    }
    int calls = 0;
};

}  // namespace

TEST_CASE("3-A: モック変換器は同期で id と結果を callback に返す") {
    MockConverter conv;
    RequestId gotId = 0;
    ConversionResult got;
    conv.Convert(42, u"kyou ha ii tenki",
                 [&](RequestId id, ConversionResult r) { gotId = id; got = r; });
    CHECK(conv.calls == 1);
    CHECK(gotId == 42);
    CHECK(got.ok);
    CHECK(got.text == std::u16string(u"kyou ha ii tenki。"));
}

TEST_CASE("3-D: 容量1（既定）は2件目を弾く") {
    ConversionQueue q;  // capacity = 1
    CHECK(q.TryEnqueue(ConversionRequest{1, u"a", ConvState::Pending, u""}));
    CHECK(q.Full());
    CHECK_FALSE(q.TryEnqueue(ConversionRequest{2, u"b", ConvState::Pending, u""}));
    CHECK(q.Size() == 1);
}

TEST_CASE("3-D: 状態遷移 Pending→Done/Failed、未知 id は false") {
    ConversionQueue q(2);
    q.TryEnqueue(ConversionRequest{1, u"a", ConvState::Pending, u""});
    q.TryEnqueue(ConversionRequest{2, u"b", ConvState::Pending, u""});
    CHECK(q.MarkDone(1, u"あ"));
    CHECK(q.MarkFailed(2));
    CHECK_FALSE(q.MarkDone(999, u"x"));  // 該当なし
}

TEST_CASE("3-D: 投入順保証 — 先頭が Pending の間は後続が Done でも pop しない") {
    ConversionQueue q(3);
    q.TryEnqueue(ConversionRequest{1, u"a", ConvState::Pending, u""});
    q.TryEnqueue(ConversionRequest{2, u"b", ConvState::Pending, u""});
    q.TryEnqueue(ConversionRequest{3, u"c", ConvState::Pending, u""});

    // 後続(2)が先に終わっても、先頭(1)がまだなら取り出せない
    CHECK(q.MarkDone(2, u"B"));
    CHECK_FALSE(q.PopReadyInOrder().has_value());

    // 先頭(1)が終わったら投入順に取り出せる
    CHECK(q.MarkDone(1, u"A"));
    auto first = q.PopReadyInOrder();
    REQUIRE(first.has_value());
    CHECK(first->id == 1);
    CHECK(first->result == std::u16string(u"A"));

    // 次は 2（既に Done）
    auto second = q.PopReadyInOrder();
    REQUIRE(second.has_value());
    CHECK(second->id == 2);

    // 3 はまだ Pending → 取り出せない
    CHECK_FALSE(q.PopReadyInOrder().has_value());
}

TEST_CASE("3-A+3-D: enqueue→変換→Done→投入順確定（3-E フローの最小形）") {
    MockConverter conv;
    ConversionQueue q;

    ConversionRequest req{7, u"kyou", ConvState::Pending, u""};
    REQUIRE(q.TryEnqueue(req));

    // 変換器を呼んで結果でキューを更新（同期）
    conv.Convert(req.id, req.source, [&](RequestId id, ConversionResult r) {
        if (r.ok) q.MarkDone(id, r.text);
        else      q.MarkFailed(id);
    });

    auto ready = q.PopReadyInOrder();
    REQUIRE(ready.has_value());
    CHECK(ready->state == ConvState::Done);
    CHECK(ready->result == std::u16string(u"kyou。"));
    CHECK(q.Empty());
}
