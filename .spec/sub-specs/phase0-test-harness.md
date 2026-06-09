# 0-C テスト土台

## 概要

`yoshinani.core`（domain + application）を対象としたユニットテストの**枠だけ**を用意する。
中身（実テスト）は 1-C・2-A・3-D で本格化する。「最後にテストが揃えばよい」方針の受け皿。

## 背景・目的

- テスト方針は厳密 TDD ではなく「**最後にテストが揃っていればよい**」。だが**テストできる構造**は最初から要る。
- TSF/COM（infra）は実機ホストが必要でユニットテストしづらい。→ 0-A により**純粋ロジックは core に隔離**済み。core だけを叩けばよい。
- v1 で「枠＋トリビアルな1テスト」だけ通しておけば、後からテストを足すコストがほぼゼロになる。

## 設計

### フレームワーク

- **doctest** を採用（単一ヘッダ・導入が最も軽い。Catch2 でも可）。
  - 採用理由: ヘッダ 1 つで完結、CMake/CTest 連携が容易、ビルドが速い。
- `tests/core/` 配下にテストを置き、`yoshinani.core` に対してリンクする。

### CMake / CTest 連携

- `yoshinani.core.tests`（EXE）を doctest と core にリンク。
- `enable_testing()` + `add_test()` で CTest 登録 → VS のテストエクスプローラ or `ctest` で実行。

### スコープ

| 対象 | テスト | 導入サブスペック |
|--|--|--|
| `TriggerPolicy`（キー意味判定） | 判定表を網羅 | 1-C |
| `RomajiKanaConverter`（決定的変換） | テーブル/促音/撥音/拗音/境界 | **2-A（主戦場）** |
| `ConversionQueue`（投入順確定） | 順序保証・MAX=1 挙動 | 3-D |
| TSF/COM（infra） | ユニットテスト対象外。受け入れは手動（メモ帳等） | 1-A/1-B |

### v1 で用意するもの

- doctest 導入 + `yoshinani.core.tests` ターゲット。
- トリビアルな疎通テスト 1 本（例: `CHECK(1 + 1 == 2);` 相当 → core の公開ヘッダを 1 つ include してビルド疎通も兼ねる）。

## ランタイム動作確認（TSF 受け入れ）— core 自動テストの外側

ユニットテストできない「実アプリで preedit が出るか」等の受け入れは、**`verify-ime` スキル**
（`.claude/skills/verify-ime/`）＋ `scripts/verify-ime.ps1` で行う。

- ヘッドレス部分（環境チェック / ビルド / regsvr32 登録・解除）は `verify-ime.ps1`（CLI 完結）。
- GUI 部分（メモ帳で実入力 → 下線 preedit / 確定 / 取消を目視）は Windows-MCP もしくは computer-use。
- **MCP や環境が未整備なら握りつぶさず実行エラーで通知して停止**（`-Action Check` が exit!=0）。
- 仮組み: `DllRegisterServer` は 1-A 実装後に有効。それまで登録は「設計通り失敗」＝そう通知する。

> 自動 ctest（core）と手動/MCP のランタイム確認の二層構成。前者で論理を、後者で TSF 結線を担保。

## 受け入れ条件

- [x] `ctest` でテストが実行でき、緑になる（`scripts/build.ps1` で実証）
- [x] doctest が core にリンクでき、core 公開ヘッダを include したテストがビルドできる
- [x] 新規テストを `tests/core/` に足すだけで CTest に乗る（`add_test` 済）
- [x] `verify-ime.ps1 -Action Check` が未整備時に exit!=0 で通知する

## 影響範囲

### 新規ファイル
- `tests/core/CMakeLists.txt`
- `tests/core/test_main.cpp`（doctest エントリ）
- `tests/core/test_trigger_policy.cpp`（最初の実テスト）
- doctest は FetchContent（ヘッダオンリー取り込み。ベンダリング不要）
- `scripts/verify-ime.ps1`, `.claude/skills/verify-ime/SKILL.md`（ランタイム動作確認）

## 依存関係（他SUBSPECとの関連）

| SUBSPEC | 関連 |
|--|--|
| 0-A | テスト対象を core に隔離した根拠 |
| 0-B | `yoshinani.core.tests` ターゲット・CTest 登録 |
| 1-C / 2-A / 3-D | 実テストの追加先 |

## TBD（未確定事項）

- doctest と Catch2 の最終選択（どちらも可。導入の軽さで doctest 仮置き）
- doctest を submodule / FetchContent / ベンダリングのどれで持つか
- 将来 infra の薄い結合テスト（IPC 疎通など）をどの枠で回すか（3-C で検討）
