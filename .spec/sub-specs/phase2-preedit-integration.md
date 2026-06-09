# 2-B preedit への結線

## 概要

2-A の `RomajiKanaConverter` を preedit に結線し、打鍵に応じて **「確定かな + pending ローマ字」** を
未確定文字列として表示する。Step1 の「ローマ字そのまま表示」を「かな表示」に差し替えるだけ。

## 背景・目的

- 1-B/1-C で composition の追記・確定・取消・トリガー（Space）は完成済み。
- ここは「preedit に入れる中身」を romaji → kana に差し替える結線点（1-B の設計意図どおり）。

## 設計

- `InputSession`（core）はローマ字列を保持し、表示文字列を `ConvertRomaji()` で生成する
  （`kana + pending` を返すメソッドを追加）。
- infra `TextService::UpdateText` は `InputSession` の表示文字列を composition に反映するだけ（変更最小）。
- Backspace の粒度（ローマ字1文字 vs かな1文字）を決める（TBD。まずローマ字1文字＝実装が素直）。
- Space 確定時、pending（未完成ローマ字）をどう扱うか決める（捨てる / そのまま出す / 強制かな化）。

## 受け入れ条件

- [ ] メモ帳で `ka`→「か」、`kya`→「きゃ」、`tta`→「った」、`nn`→「ん」が preedit 表示される
- [ ] Space で確定したかなが流れる（pending の扱いは決めた仕様どおり）
- [ ] Backspace / Esc が新表示でも破綻しない

## 影響範囲

### 修正
- `src/core/application/InputSession.{h,cpp}`（romaji 保持 + 表示文字列生成）
- `src/tsf/TextService.cpp`（UpdateText が表示文字列を使う・Backspace 粒度）

## 依存関係

| SUBSPEC | 関連 |
|--|--|
| 2-A | 変換ロジック |
| 1-B/1-C | composition / トリガー |
| 3-E | 確定時に reading（かな）を zenz へ渡す |

## TBD

- Backspace 粒度（ローマ字 / かな）
- Space 確定時の pending 処理
- InputSession が romaji と kana の両方を持つか、romaji のみ保持し表示で都度変換するか
