// 3-A 変換ポート抽象 — domain（OS非依存・実装に依存しない）。
// ひらがな/ローマ字 → 漢字交じり文 の変換を「ポート」として定義する。
// core はこのポートにのみ依存し、実装（パイプ/デーモン）は infra（3-C）に置く＝依存性逆転。
// request_id 付きの非同期コールバック形（§6.5 ①「形だけ非同期」）。v1 実装は同期で即 callback を呼んでよい。
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace yoshinani::core::domain {

// 変換リクエストの識別子（投入順・結果突合に使う）。
using RequestId = std::uint64_t;

// 変換結果。ok=false は変換失敗（呼び出し側はフォールバック＝生入力のまま確定する／3-E）。
struct ConversionResult {
    std::u16string text;   // 変換後テキスト（UTF-16）
    bool ok = false;
};

// 変換ポート。input（B方式では空白区切りローマ字）を渡し、結果を callback で受ける。
class IKanaKanjiConverter {
public:
    virtual ~IKanaKanjiConverter() = default;

    // 変換依頼。結果は onDone(id, result) で返す。
    //   v1 実装は内部で同期往復して即 onDone を呼んでよい（§6.5 ①）。
    //   将来 async 化（4-A）では「待たずに後で onDone」に変えるだけで core 側は不変。
    virtual void Convert(RequestId id,
                         std::u16string_view input,
                         std::function<void(RequestId, ConversionResult)> onDone) = 0;
};

}  // namespace yoshinani::core::domain
