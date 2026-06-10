# 1-D 入力モード切替（変換モード / 直接入力モード — Google 日本語入力準拠）

## 概要

「よしなに」ON のまま、**変換対応モード**（ローマ字/記号を preedit に溜めて Tab で Gemma 変換）と
**直接入力モード**（全キー素通し＝英数記号をそのまま入力）を**キーマップで切り替える**。
コーディング時は直接入力、文章入力時は変換モード、を IME を切り替えずに行き来する。

## 背景・目的

- 要望(2026-06-09): このIMEのままコーディングもしたい。Google 日本語入力同様に
  「ひらがな(変換) / 英数(直接)」をキーで切り替えたい。
- TSF 的には **キーボード open/close コンパートメント**（`GUID_COMPARTMENT_KEYBOARD_OPENCLOSE`）に対応する概念。
  - open = 変換モード（TIP が入力を食う）/ close = 直接入力（食わない）。
- ★ **副次効果（重要・HANDOFF §2 既知課題に直結）**: open/close 未設定が、Win11 新メモ帳など
  標準アプリで TIP が engage しない原因の有力候補。本機能で open/close を正しく実装すれば
  **標準アプリ互換も改善する可能性**がある（要検証）。

## 設計（方針・TBD 多め）

- モード状態を持つ（既定は要検討：変換 or 直接）。状態は **core/application（OS非依存・テスト可）** に置く。
- 切替キーは settings.json の `triggerKeys` と同様に **`modeToggleKeys` として core/Settings に分離**。
  - 既定候補: 半角/全角キー（`VK_KANJI` / `VK_OEM_AUTO` 等）/ `Ctrl+Space` / Google IME 互換キー。
- `Classify`/`Decide` の**前段**で「直接入力モードなら即 `PassThrough`」。
- TSF の `GUID_COMPARTMENT_KEYBOARD_OPENCLOSE` とモード状態を**同期**させ、言語バー/アプリにモードを正しく見せる。
- モード表示（あ/A 相当インジケータ）は将来（4-B 付近）。

## 受け入れ条件

- [ ] 切替キーで「変換モード ⇄ 直接入力モード」が切り替わる
- [ ] 直接入力モードでは英数記号がそのまま入る（preedit を作らない）＝コーディング可
- [ ] 変換モードでは従来どおり preedit→Tab 変換
- [ ] （調査）open/close 実装で標準アプリ（新メモ帳）の engage が改善するか検証

## 影響範囲

### 新規
- `src/core/application/InputMode.{h,cpp}`（モード状態・切替判定。OS非依存・テスト可）
- `src/core/application/Settings` に `modeToggleKeys` 追加

### 修正
- `src/tsf/TextService.cpp`（モード参照で素通し分岐 + open/close コンパートメント連動）
- `src/tsf/KeyMap.cpp`（切替キー名→VK）

## 依存関係

| SUBSPEC | 関連 |
|--|--|
| 1-B / 1-C | 入力・確定経路の前段にモード分岐を挟む |
| HANDOFF §2 既知課題 | open/close 実装が標準アプリ engage 改善の有力候補 |

## TBD

- ~~既定モード（変換 or 直接）~~ → **変換モード**（2026-06-10 ユーザー確定）
- ~~切替キーの既定（半角/全角 / Ctrl+Space / その他）~~ → **半角/全角キー**（2026-06-10 ユーザー確定。settings.json `modeToggleKeys` で変更可能にする）
- open/close コンパートメントと TIP 自前モード状態の二重管理を一本化する方法
- モードのスコープ（スレッド単位 / グローバル / アプリ単位）
