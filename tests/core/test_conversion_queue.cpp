// 3-A/3-D 変換ポート & 変換キューのユニットテスト（モック変換器使用）。
#include <doctest/doctest.h>

#include <functional>
#include <string>
#include <string_view>

#include "application/ConversionQueue.h"
#include "application/PreeditView.h"
#include "domain/ports/IKanaKanjiConverter.h"

using yoshinani::core::application::ConversionQueue;
using yoshinani::core::application::ConversionRequest;
using yoshinani::core::application::ConvState;
using yoshinani::core::domain::ConversionInput;
using yoshinani::core::domain::ConversionResult;
using yoshinani::core::domain::IKanaKanjiConverter;
using yoshinani::core::domain::RequestId;

namespace {

// source をそのまま「<source>。」に変換するだけの同期モック（3-A 実装の代役）。
class MockConverter : public IKanaKanjiConverter {
public:
    void Convert(RequestId id, const ConversionInput& input,
                 std::function<void(RequestId, ConversionResult)> onDone) override {
        ++calls;
        lastContext = input.context;
        onDone(id, ConversionResult{input.source + u"。", true});
    }
    int calls = 0;
    std::u16string lastContext;
};

}  // namespace

TEST_CASE("3-A: モック変換器は同期で id と結果を callback に返す（文脈も受け取る）") {
    MockConverter conv;
    RequestId gotId = 0;
    ConversionResult got;
    conv.Convert(42, ConversionInput{u"kyou ha ii tenki", u"昨日は雨だった"},
                 [&](RequestId id, ConversionResult r) { gotId = id; got = r; });
    CHECK(conv.calls == 1);
    CHECK(gotId == 42);
    CHECK(got.ok);
    CHECK(got.text == std::u16string(u"kyou ha ii tenki。"));
    CHECK(conv.lastContext == std::u16string(u"昨日は雨だった"));  // 継続モードの文脈伝搬
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

TEST_CASE("4-A: 容量8 — Pending 8件で満杯（満杯時 Tab 無視の根拠）") {
    ConversionQueue q(8);
    for (int i = 1; i <= 8; ++i) {
        CHECK(q.TryEnqueue(
            ConversionRequest{static_cast<RequestId>(i), u"a", ConvState::Pending, u""}));
    }
    CHECK(q.Full());
    CHECK_FALSE(q.TryEnqueue(ConversionRequest{9, u"x", ConvState::Pending, u""}));
    CHECK(q.Size() == 8);
}

TEST_CASE("1-D: Full は Pending 件数で測る — 確定済みリテラルは数えない") {
    ConversionQueue q(2);
    q.TryEnqueue(ConversionRequest{1, u"a", ConvState::Pending, u""});
    q.PushCommitted(100, u"\n");
    q.PushCommitted(101, u"\n");
    q.PushCommitted(102, u"raw");
    CHECK_FALSE(q.Full());  // Pending は1件のみ → まだ満杯でない
    q.TryEnqueue(ConversionRequest{2, u"b", ConvState::Pending, u""});
    CHECK(q.Full());        // Pending 2件で満杯
}

TEST_CASE("1-D: PushCommitted は投入順に確定される（改行の位置厳守）") {
    ConversionQueue q(8);
    q.TryEnqueue(ConversionRequest{1, u"kyou", ConvState::Pending, u""});  // 変換中
    q.PushCommitted(2, u"raw");                                            // 生確定
    q.PushCommitted(3, u"\n");                                             // 改行

    // 先頭(1)が Pending の間は、後ろのリテラルも取り出せない（追い越し禁止＝位置厳守）。
    CHECK_FALSE(q.PopReadyInOrder().has_value());

    // 先頭が変換完了 → 投入順に 変換結果 → raw → 改行 で取り出せる。
    CHECK(q.MarkDone(1, u"今日"));
    auto a = q.PopReadyInOrder();  REQUIRE(a.has_value()); CHECK(a->result == std::u16string(u"今日"));
    auto b = q.PopReadyInOrder();  REQUIRE(b.has_value()); CHECK(b->result == std::u16string(u"raw"));
    auto c = q.PopReadyInOrder();  REQUIRE(c.has_value()); CHECK(c->result == std::u16string(u"\n"));
    CHECK(q.Empty());
}

TEST_CASE("4-A: Clear で全破棄 — 破棄済み id の遅延 MarkDone は無視される") {
    ConversionQueue q(8);
    q.TryEnqueue(ConversionRequest{1, u"a", ConvState::Pending, u""});
    q.TryEnqueue(ConversionRequest{2, u"b", ConvState::Pending, u""});
    q.Clear();
    CHECK(q.Empty());
    CHECK_FALSE(q.MarkDone(1, u"A"));  // Esc 後に届いた結果は無視（4-A の全取消）
    CHECK_FALSE(q.PopReadyInOrder().has_value());
}

TEST_CASE("4-A: PreeditView — 変換待ちソース連結 + 打鍵中、変換中区間長") {
    using yoshinani::core::application::BuildPreeditView;
    ConversionQueue q(8);
    q.TryEnqueue(ConversionRequest{1, u"kyou ha ", ConvState::Pending, u""});
    q.TryEnqueue(ConversionRequest{2, u"tenki ", ConvState::Pending, u""});

    auto v = BuildPreeditView(q, u"ga ii");
    CHECK(v.text == std::u16string(u"kyou ha tenki ga ii"));
    CHECK(v.convertingLen == 14);  // "kyou ha tenki " まで

    // 空キュー・打鍵のみ → 変換中区間なし
    q.Clear();
    auto v2 = BuildPreeditView(q, u"abc");
    CHECK(v2.text == std::u16string(u"abc"));
    CHECK(v2.convertingLen == 0);

    // 全空
    auto v3 = BuildPreeditView(q, u"");
    CHECK(v3.text.empty());
    CHECK(v3.convertingLen == 0);
}

TEST_CASE("3-A+3-D: enqueue→変換→Done→投入順確定（3-E フローの最小形）") {
    MockConverter conv;
    ConversionQueue q;

    ConversionRequest req{7, u"kyou", ConvState::Pending, u""};
    REQUIRE(q.TryEnqueue(req));

    // 変換器を呼んで結果でキューを更新（同期）
    conv.Convert(req.id, ConversionInput{req.source, u""},
                 [&](RequestId id, ConversionResult r) {
        if (r.ok) q.MarkDone(id, r.text);
        else      q.MarkFailed(id);
    });

    auto ready = q.PopReadyInOrder();
    REQUIRE(ready.has_value());
    CHECK(ready->state == ConvState::Done);
    CHECK(ready->result == std::u16string(u"kyou。"));
    CHECK(q.Empty());
}
