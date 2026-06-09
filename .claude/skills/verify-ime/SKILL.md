---
name: verify-ime
description: >-
  よしなに(Yoshinani) IME のランタイム動作確認を自己完結ループで行う仮組みスキル。
  ビルド → regsvr32 登録 → メモ帳で実キー入力 → 下線付き preedit / トリガー確定 / Esc取消 を
  目視確認 → 後始末（登録解除）までを Windows-MCP もしくは computer-use で駆動する。
  「動作確認して」「IMEが効くか確認」「preeditが出るか見て」「受け入れ条件を検証して」
  と言われた場合に使用する。該当 MCP ツールや環境が未整備なら、握りつぶさず実行エラーとして
  ユーザに通知して停止する。
---

# verify-ime — よしなに IME ランタイム動作確認（仮組み）

> ⚠ **仮組み（Provisional）**: TSF DLL の COM 登録（`DllRegisterServer`）は **1-A 実装後**に有効。
> それ以前は登録ステップが「設計通り失敗」する。その場合も**実行エラーとして通知**し、
> 黙って成功扱いにしない。

`yoshinani.core` のロジックは `ctest` で自動検証できる（→ `scripts/build.ps1`）。
本スキルが扱うのは**自動テストできない TSF ランタイム受け入れ**（実アプリで preedit が出るか）だけ。

## 使うツール（前提）

- ヘッドレス部分: `scripts/build.ps1` / `scripts/verify-ime.ps1`（PowerShell, CLI 完結）
- GUI 操作部分: **Windows-MCP**（`mcp__Windows-MCP__*`）を第一候補、無ければ **computer-use**（`mcp__computer-use__*`）
- どちらの MCP も使えない場合 → **下記「エラー通知」に従い停止**

## 手順

### 0. 前提チェック（必須・最初に必ず実行）

```
pwsh -File scripts/verify-ime.ps1 -Action Check
```

- 終了コード != 0、または出力に `!` がある → **その内容をユーザにそのまま提示して停止**（先に進まない）。
- 同時に、GUI 駆動 MCP の利用可否を確認する:
  - Windows-MCP / computer-use のツールが**ツール一覧に無い / 接続されていない**場合は
    → 「エラー通知」へ。**メモ帳操作に進まない。**

### 1. ビルド & 登録（ヘッドレス）

```
pwsh -File scripts/verify-ime.ps1 -Action Build
pwsh -File scripts/verify-ime.ps1 -Action Register   # 管理者権限が必要
```

- `Register` が「DllRegisterServer を持ちません（1-A 未実装）」で失敗したら
  → **それは現状の正常な結果**。「1-A 未実装のためランタイム確認は保留」とユーザに通知して停止。

### 2. メモ帳で実入力（GUI / MCP）

Windows-MCP（無ければ computer-use）で:

1. メモ帳を起動（`mcp__Windows-MCP__App` / computer-use `open_application`）。
2. IME を「よしなに」へ切替（言語バー or `Win`+`Space`）。
3. ローマ字を数文字入力。
4. **スクリーンショットで下線付き preedit が出ているか確認**（`mcp__Windows-MCP__Screenshot` / computer-use `screenshot`）。
5. `。`（または `Shift+Tab`）を送出 → **下線が消え、打った文字が確定**していることを確認。
6. もう一度入力して `Esc` → **preedit が消え、確定されない**ことを確認。

### 3. 受け入れ条件の判定（MAIN_SPEC §7 / 1-A〜1-C）

| # | 条件 | 確認方法 |
|--|--|--|
| 1 | IME 一覧に「よしなに」 | `-Action Register` 成功後、設定/言語バー |
| 2 | 英字で下線付き preedit | 手順2-4 スクショ |
| 3 | トリガーで確定 | 手順2-5 |
| 4 | Esc で取消（非確定） | 手順2-6 |
| 5 | IME 切替で落ちない | 切替を数回繰り返す |

各条件を ✓/✗ で報告。✗ があれば**現象・スクショ・推定原因**を添える。

### 4. 後始末（必須）

```
pwsh -File scripts/verify-ime.ps1 -Action Unregister
```

- システムに IME を登録したまま放置しない。失敗時もユーザに通知。

## エラー通知（環境・ツール未整備時）

以下はいずれも**握りつぶさず、実行エラーとしてユーザに通知して停止**する（CLAUDE.md フォールバック方針）。

| 状況 | 通知内容（例） |
|--|--|
| Windows-MCP も computer-use も無い | 「GUI 動作確認に必要な MCP（Windows-MCP / computer-use）が接続されていません。接続するか、ヘッドレス部分（ctest）までで判断します。」 |
| `verify-ime.ps1 -Action Check` が失敗 | スクリプトの `!` 行をそのまま提示し、解消方法（ビルド/管理者シェル等）を案内 |
| `DllRegisterServer` 未エクスポート | 「TSF DLL が未だ COM 登録未対応（1-A 未実装）。ランタイム確認は 1-A 完了後に実施可能。」 |
| 非管理者で Register | 「regsvr32 登録には管理者権限が必要。管理者 PowerShell で再実行してください。」 |
| regsvr32 失敗 | exit コードと、`-Action Unregister` での巻き戻し手順を提示 |

> 原則: **「動かなかった」を「動いた」と報告しない。** 検証できない場合はその旨と、ユーザが手で叩ける最小コマンドを示す。
