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

// 変換依頼の入力（継続モード対応・2026-06-10）。
//   context は「直前に確定した文」（空可）。LazyJP の文脈チェイン相当で、
//   実装はこれをプロンプトの文脈節に入れて変換精度を上げる（出力には含めない）。
struct ConversionInput {
    std::u16string source;   // 変換対象（B方式では romaji。空白あり/なし両対応）
    std::u16string context;  // 直前の確定文（参考情報。空なら文脈なし）
};

// 変換ポート。結果を callback で受ける。
class IKanaKanjiConverter {
public:
    virtual ~IKanaKanjiConverter() = default;

    // 変換依頼。結果は onDone(id, result) で返す。
    //   v1 実装は内部で同期往復して即 onDone を呼んでよい（§6.5 ①）。
    //   4-A では呼び出し側（ConvertMarshaller）がワーカースレッドで実行する。
    virtual void Convert(RequestId id,
                         const ConversionInput& input,
                         std::function<void(RequestId, ConversionResult)> onDone) = 0;
};

}  // namespace yoshinani::core::domain
