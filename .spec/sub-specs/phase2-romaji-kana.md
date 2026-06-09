# 2-A romaji→kana 決定的変換

## 概要

ローマ字をひらがなへ**決定的に**変換する（モデル不要）。テーブル＋促音/撥音/拗音ルール。
core domain の純粋ロジックで、**ユニットテストの主戦場**。これだけで「ローマ字かな入力」が成立する（MAIN_SPEC Step2）。

## 背景・目的

- MAIN_SPEC §4: `romaji→kana` は「自前の決定的変換（テーブル＋促音/撥音ルール）」。
- OS非依存・副作用なし → 0-A 方針どおり core に置き、ctest で自走検証できる。
- Step3（zenz）の入力 reading を作る前段。

## 設計

### domain: `RomajiKanaConverter`（純粋・テスト対象）

ローマ字列を「確定したかな」と「まだ確定できない末尾ローマ字（pending）」に分ける。
逐次入力に耐えるよう、未完成な綴り（`k`, `ky`, `n`）は pending として保持する。

```cpp
// src/core/domain/RomajiKanaConverter.h（概念）
namespace yoshinani::core::domain {
struct KanaResult {
    std::u16string kana;     // 確定したひらがな
    std::u16string pending;  // まだかなにできない末尾ローマ字
};
KanaResult ConvertRomaji(std::u16string_view romaji);
}
```

### 変換ルール（最低限）

| 種別 | 例 |
|--|--|
| 基本 | `a→あ`, `ka→か`, `shi/si→し`, `chi/ti→ち`, `tu/tsu→つ`, `fu/hu→ふ` |
| 拗音 | `kya→きゃ`, `sha→しゃ`, `jya/ja→じゃ` |
| 促音 | 子音重ね `kka→っか`, `tte→って` |
| 撥音 | `nn→ん`, `n`+子音 → `ん`（`konnnichiha` 系の n 確定規則） |
| 小書き | `xa/la→ぁ`, `xtu/ltu→っ` |
| 長音/記号 | `-→ー`（任意）, 未定義はそのまま pending or 透過（TBD） |

### 表示への反映（2-B で結線）

preedit 表示 = `kana + pending`。InputSession はローマ字を保持し、本変換で表示文字列を都度生成する。

## 受け入れ条件

- [ ] 主要ローマ字→かなのユニットテストが網羅されている（清音/濁音/半濁音/拗音/促音/撥音/小書き）
- [ ] `n` の確定規則（`na` vs `nn` vs `n`+子音）をテストで固定
- [ ] 未完成綴りが pending として返る（`k`→kana空/pending"k"）
- [ ] 逐次追記しても結果が一貫する

## 影響範囲

### 新規
- `src/core/domain/RomajiKanaConverter.{h,cpp}`
- `tests/core/test_romaji_kana.cpp`

## 依存関係（他SUBSPEC）

| SUBSPEC | 関連 |
|--|--|
| 1-B/1-C | InputSession / preedit に結線（2-B） |
| 2-B | 表示結線 |
| 3-E | 確定時の reading（かな）を本変換で得る |

## TBD（未確定事項）

- `n` の確定タイミング（次キーで確定 vs 即時）と、トリガー確定時の pending 残処理
- 未定義ローマ字・大文字・記号の扱い（透過 / そのまま / 無視）
- テーブルの持ち方（コードに埋め込み vs データ）。将来 settings で配列入力方式を切替可能にするか
