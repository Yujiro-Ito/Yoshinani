# 3-D 変換キュー & 投入順確定（§6.5 ②③）

## 概要

変換待ちを**リスト**で持ち（§6.5 ②, v1 は MAX=1）、確定は**投入順キュー**経由で行う（§6.5 ③, v1 は素通り）。
将来の async 化（4-A）に向けた「形」を v1 で仕込む。

## 背景・目的

- MAIN_SPEC §6.5: ② 変換待ちを `{id, reading, state}` のリストで持つ（器は N 件、v1 は最大1）。
  ③ 確定は投入順キュー経由（v1 は1件なので素通り、順序保証の口だけ開ける）。
- 1-C で「確定は CommitCurrent を必ず経由」を作ってある＝ここに差し込む。

## 設計

```cpp
// src/core/application（概念）
enum class ConvState { Pending, Done, Failed };
struct ConversionRequest { RequestId id; std::u16string reading; ConvState state; std::u16string result; };

class ConversionQueue {     // v1: 容量1。順序保証の口だけ用意
public:
    bool TryEnqueue(ConversionRequest);  // 満杯(=1件)なら false（v1）
    // 投入順に「確定可能になった先頭」を取り出す
    std::optional<ConversionRequest> PopReadyInOrder();
};
```

- v1: `MAX=1`。enqueue→即変換（3-A/3-C 経由）→結果で state=Done→投入順に確定。
- 確定は 1-C の `CommitCurrent` 経路を通す（複数箇所から EndComposition しない）。

## 受け入れ条件

- [ ] キューの**投入順保証**と MAX=1 のユニットテスト（モック変換器使用）
- [ ] state 遷移（Pending→Done/Failed）のテスト
- [ ] 確定が CommitCurrent 経路を通る

## 影響範囲

### 新規
- `src/core/application/ConversionQueue.{h,cpp}` / `ConversionRequest`
- `tests/core/test_conversion_queue.cpp`

## 依存関係

| SUBSPEC | 関連 |
|--|--|
| 3-A | ポート経由で変換 |
| 3-E | 確定の結線 |
| 4-A | `MAX=1` を外す＝async 化の本番 |

## TBD

- `MAX=1` 撤廃時の未確定保持（B方式）との整合
- Failed 時の扱い（かな確定フォールバック）
