# 3-E A 自動確定の結線（プロダクト完成）

## 概要

トリガー（Space）で **reading(かな) → zenz で漢字化 → そのまま自動確定して手放す**。
これで MAIN_SPEC の A 方式（一方通行・戻らない）が完成し、**Step3＝プロダクトとして完結**する。

## 背景・目的

- MAIN_SPEC §1: A 確定（トリガー＝変換＋自動確定＋手放す）。
- ここまでの部品（2-A reading、3-A ポート、3-C パイプ、3-D キュー）を確定経路に結線する。

## 設計

- トリガー確定時のフロー（同期・v1）:
  1. preedit の reading（かな）を取得（2-A/2-B）
  2. `ConversionQueue.TryEnqueue` → `IKanaKanjiConverter.Convert`（3-A/3-C）
  3. 結果 kanji を composition に反映 → `EndComposition`（確定して手放す）
- 失敗/デーモン未起動時は **かなのまま確定**（フォールバック・握りつぶさない）。
- 1-C の `CommitCurrent` 経路に乗せる（3-D の投入順キュー経由）。

## 受け入れ条件（プロダクト完成の証明）

- [ ] メモ帳で `kyouhaiitenki` + `Space` → 「今日はいい天気」相当が確定（zenz 稼働時）
- [ ] デーモン未起動時はかな確定にフォールバックし、IME が固まらない
- [ ] 確定後すぐ次の入力に移れる（手放す）

## 影響範囲

### 修正
- `src/tsf/TextService.cpp`（Commit 経路で変換→確定）
- `src/core/application`（InputSession / ConversionQueue 結線）

## 依存関係

| SUBSPEC | 関連 |
|--|--|
| 2-A/2-B | reading 生成 |
| 3-A/3-C | 変換ポート・パイプ |
| 3-D | 投入順確定 |
| 4-A | async 化（待たない確定） |

## TBD

- 変換レイテンシの体感（同期で待つ時間）→ 4-A の動機
- フォールバック時の見せ方（無音でかな確定 vs 通知）
