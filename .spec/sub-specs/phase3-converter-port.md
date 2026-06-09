# 3-A 変換ポート抽象（§6.5 ①）

## 概要

ひらがな→漢字の変換を、**`IKanaKanjiConverter` ポート**として domain に定義する。
**request_id 付きの非同期インターフェース**にする（MAIN_SPEC §6.5 ①「形だけ非同期」）。
core はこのポートにのみ依存し、実装（zenz/パイプ）は infra（3-C）に置く＝依存性逆転。

## 背景・目的

- MAIN_SPEC §3: LLM を TIP に同居させない。core は「変換を頼んで結果文字列を受け取る」だけ。
- §6.5 ①: v1 は「投げて即待つ（実質同期）」だが、**形は非同期コールバック**にしておく
  → 将来 async 化が「待たない」に変えるだけで済む。

## 設計

```cpp
// src/core/domain/ports/IKanaKanjiConverter.h（概念）
namespace yoshinani::core::domain {
using RequestId = uint64_t;
struct ConversionResult { std::u16string text; bool ok; };

class IKanaKanjiConverter {
public:
    virtual ~IKanaKanjiConverter() = default;
    // reading(ひらがな) を変換依頼。結果は callback で返す（v1 は内部で即時呼んでよい）。
    virtual void Convert(RequestId id, std::u16string_view reading,
                         std::function<void(RequestId, ConversionResult)> onDone) = 0;
};
}
```

- v1 実装（3-C `PipeKanaKanjiConverter`）は同期的に往復して即 callback を呼ぶ。
- Step3 は候補トップ1（`text`）で十分。複数候補は将来。

## 受け入れ条件

- [ ] `IKanaKanjiConverter` がポートとして定義され、core が実装に依存しない
- [ ] モック実装（reading→固定漢字）で 3-D キュー / 確定経路がユニットテストできる

## 影響範囲

### 新規
- `src/core/domain/ports/IKanaKanjiConverter.h`
- `tests/core/test_conversion_queue.cpp`（モックで使用）

## 依存関係

| SUBSPEC | 関連 |
|--|--|
| 3-C | パイプ実装 |
| 3-D | キューがこのポートを呼ぶ |
| 4-A | async 化（callback を「待たない」へ） |

## TBD

- 結果型（トップ1文字列 vs 候補リスト）
- callback のスレッド（v1 は呼び出しスレッド同期）
- タイムアウト / 失敗時のセマンティクス（→ 3-E でフォールバック）
