// 3-D 変換キュー & 投入順確定（§6.5 ②③）— application（OS非依存・テスト対象）。
// 変換待ちを器（v1 は容量1）で持ち、確定は「投入順」に取り出す。
// v1 は MAX=1 で素通り同然だが、async 化（4-A）に向けて順序保証の口を先に用意する。
//
// 本クラスは受動的なデータ構造（enqueue / 結果マーク / 投入順 pop）に徹する。
// 実際に IKanaKanjiConverter.Convert を呼ぶオーケストレーションは呼び出し側（3-E）が持つ。
#pragma once
#include <cstddef>
#include <deque>
#include <optional>
#include <string>

#include "domain/ports/IKanaKanjiConverter.h"  // RequestId

namespace yoshinani::core::application {

using RequestId = domain::RequestId;

enum class ConvState { Pending, Done, Failed };

struct ConversionRequest {
    RequestId id{};
    std::u16string source;                 // 変換元（B方式＝空白区切りローマ字）
    ConvState state = ConvState::Pending;
    std::u16string result;                 // Done のとき変換結果
};

class ConversionQueue {
public:
    // v1 は容量1。順序保証のテスト/将来拡張のため容量は可変にしてある。
    explicit ConversionQueue(std::size_t capacity = 1) : capacity_(capacity) {}

    bool Empty() const noexcept { return items_.empty(); }
    std::size_t Size() const noexcept { return items_.size(); }
    bool Full() const noexcept { return items_.size() >= capacity_; }

    // 満杯なら false（v1: 容量1なので未確定が1件あると弾く）。
    bool TryEnqueue(ConversionRequest req);

    // id（Pending のもの）を Done/Failed に遷移。該当する Pending が無ければ false
    //   （未知 id、または既に終端状態＝二重マーク防止）。状態遷移は一方向。
    bool MarkDone(RequestId id, std::u16string result);
    bool MarkFailed(RequestId id);

    // 投入順に「先頭が終端状態(Done/Failed)なら」取り出す。
    //   先頭がまだ Pending なら（後続が Done でも）nullopt＝順序を追い越させない。
    std::optional<ConversionRequest> PopReadyInOrder();

    // 全件破棄（Esc の全取消・4-A）。以後、破棄済み id の MarkDone/MarkFailed は
    // false を返すだけになる＝遅れて届く変換結果は自然に無視される。
    void Clear() noexcept { items_.clear(); }

    // 表示合成用の読み取りアクセス（preedit 連結表示・4-A/4-B）。
    const std::deque<ConversionRequest>& Items() const noexcept { return items_; }

private:
    std::size_t capacity_;
    std::deque<ConversionRequest> items_;
};

}  // namespace yoshinani::core::application
