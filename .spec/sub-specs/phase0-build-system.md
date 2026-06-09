# 0-B ビルド / プロジェクト構成

## 概要

CMake を基線にしたビルド構成を定義する。Visual Studio の CMake サポートで開いて F5 ビルド/デバッグできる前提。
0-A のレイヤ分割を **CMake ターゲット分割** として実体化し、「core が Windows に依存しない」をビルドで強制する。

## 背景・目的

- なぜ CMake か:
  - 仕様が **llama.cpp（CMake 製）** と **Android 再利用** を含む。VS .sln 単体だと llama.cpp 統合と Android 展開が後で破綻しやすい。
  - CMake なら llama.cpp を `FetchContent` / `add_subdirectory` で素直に取り込め、**VS は CMakeLists をネイティブに開ける**ため F5・IntelliSense・デバッガはそのまま使える（＝セットアップの楽さは VS .sln とほぼ同等）。
  - ターゲットごとに include ディレクトリ・リンク依存を制御でき、レイヤ境界をコンパイル時に守れる。

## 設計

### ターゲット構成

| ターゲット | 種別 | レイヤ | 依存 | 備考 |
|--|--|--|--|--|
| `yoshinani.core` | STATIC | domain + application | （なし） | **Windows SDK を一切リンク/インクルードしない** |
| `yoshinani.ipc` | STATIC | infra | core | 名前付きパイプ実装（3-C） |
| `yoshinani.tsf` | SHARED/MODULE | infra | core, ipc, Windows SDK | 出力 = **`yoshinani.dll`**。`.def` で COM エクスポート |
| `yoshinanid` | EXE | infra | llama.cpp | zenz デーモン（3-B）。core 非依存でも可 |
| `yoshinani.core.tests` | EXE | test | core, doctest | 0-C |

```
yoshinani.core ◀── yoshinani.ipc ◀── yoshinani.tsf (= yoshinani.dll)
       ▲                                   │
       └────────────── yoshinani.core.tests┘
yoshinanid ── llama.cpp（独立 exe・別プロセス）
```

### core を OS 非依存に保つ強制

- `yoshinani.core` の `target_link_libraries` に Windows ライブラリを入れない。
- `target_include_directories` に Windows SDK / TSF ヘッダパスを通さない（PRIVATE で最小）。
- 方針: core 内で `#include <windows.h>` 等を書くと **未解決/未定義でコンパイルエラー**になる状態を維持する。
  （必要なら CI で `grep -r "windows.h\|msctf.h" src/core` が空であることをチェックする軽いガードを足す。）

### DLL エクスポート（TSF TIP の COM サーバ要件）

`yoshinani.dll` は標準 COM エクスポートを `.def` で公開する（1-A で実装）:

```
EXPORTS
    DllGetClassObject   PRIVATE
    DllCanUnloadNow     PRIVATE
    DllRegisterServer   PRIVATE
    DllUnregisterServer PRIVATE
```

### ツールチェーン / 共通設定

| 項目 | 値 |
|--|--|
| 言語標準 | C++20（`std::expected` を使うなら C++23 / MSVC `/std:c++latest`） |
| コンパイラ | MSVC（VS Build Tools。IDE は不要） |
| アーキ | x64（将来 ARM64 を見据えるが v1 は x64） |
| 文字コード | `/utf-8`。UNICODE / _UNICODE 定義（TSF は Wide API） |
| 警告 | `/W4 /permissive-`。core は警告厳しめ |

### 実測ツールチェーン（このPC・2026-06-09 時点）と落とし穴

検証環境を実測し、**自己完結ループ（生成→ビルド→ctest）が緑になることを確認済み**。

| 項目 | 実測値 / 方針 |
|--|--|
| VS | **Build Tools 2019 のみ**（IDE 無し）。`cl.exe` MSVC 14.29、Windows SDK 10.0.19041（`msctf.h` あり） |
| CMake / Ninja | **VS 同梱の Microsoft パッチ版 CMake 3.20 は使わない**（Ninja 生成にバグ）。`pip install cmake ninja` で導入した **CMake 4.3.2 / Ninja 1.13.0** を使う |
| ビルド入口 | `scripts/build.ps1`（vcvars64 を都度読込 → cmake → ninja → ctest を 1 コマンド化）。VS IDE 不要＝完全 CLI 完結 |

**踏んだ落とし穴（再発防止）:**
1. **VS 同梱 CMake 3.20** は `rules.ninja` のルール名に未展開トークンを出し ninja がパース不能 → 現行版 CMake を入れて回避。
2. **CMake 4.x は `cmake_minimum_required(VERSION < 3.5)` を拒否** → doctest はヘッダオンリーとして取り込み、doctest 自身の CMake プロジェクトは構成しない（`tests/core/CMakeLists.txt`）。
3. **PowerShell は裸の native 引数 `-DCMAKE_BUILD_TYPE=$X` を展開しない場合がある** → 必ず `"-DCMAKE_BUILD_TYPE=$X"` とダブルクォートで構築する。

### llama.cpp 取り込み（3-B 用・形だけ先に決める）

- 方式候補: `FetchContent_Declare(llama ...)` か git submodule。**`yoshinanid` だけが依存**し、core/tsf には波及させない。
- GGUF モデル本体はリポジトリに含めない（`.gitignore` で `*.gguf` 除外済み）。実行時に既知パスへ配置 or 起動引数で渡す。

### 配置（0-A のディレクトリ構成に対応）

```
CMakeLists.txt              # ルート。各 add_subdirectory
src/core/CMakeLists.txt
src/tsf/CMakeLists.txt
src/ipc/CMakeLists.txt
src/daemon/CMakeLists.txt
tests/core/CMakeLists.txt
CMakePresets.json           # x64-debug / x64-release プリセット（VS が読む）
```

## 受け入れ条件

- [x] `pwsh -File scripts/build.ps1` で configure が成功する
- [x] 全ターゲットがビルドできる（core / ipc / tsf / tests）
- [x] `yoshinani.tsf` が `yoshinani.dll` を出力する
- [x] `yoshinani.core` のビルドに Windows SDK リンクが不要（core 単体で通る）
- [x] `ctest` が緑（`TriggerPolicy` 判定表テスト）
- [ ] （任意）VS IDE で CMake フォルダを開いて F5 デバッグ（IDE 導入時。ホストアタッチ手順は 1-A）
- [ ] `yoshinanid`（daemon）は 3-B まで OFF のまま（`YOSHINANI_BUILD_DAEMON`）

## 影響範囲

### 新規ファイル
- `CMakeLists.txt`（ルート）, `CMakePresets.json`
- `src/{core,tsf,ipc,daemon}/CMakeLists.txt`, `tests/core/CMakeLists.txt`
- `src/tsf/yoshinani.def`

## 依存関係（他SUBSPECとの関連）

| SUBSPEC | 関連 |
|--|--|
| 0-A | レイヤ→ターゲットの対応元。依存方向の根拠 |
| 0-C | `yoshinani.core.tests` ターゲットと CTest 登録 |
| 1-A | `.def` の COM エクスポートを実装、DLL 登録手順 |
| 3-B | llama.cpp 取り込み方式をここで確定 |

## TBD（未確定事項）

- llama.cpp 取り込みを FetchContent / submodule どちらにするか（ビルド時間とバージョン固定のトレードオフ）
- `yoshinani.tsf` を SHARED と MODULE どちらで作るか（COM in-proc サーバとしてはどちらでも可。VS デバッグのしやすさで判断）
- core の標準文字型（UTF-8 / UTF-16）に伴うビルド定義（0-A の TBD と同期）
- C++23（`std::expected`）を採用するか C++20 に留めるか
