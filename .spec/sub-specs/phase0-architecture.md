# 0-A アーキテクチャ方針（DDD レイヤ + コア/アダプタ分離）

## 概要

よしなに全体の **設計の憲法**。レイヤ分割・依存方向・所有権の方針を定め、以降の全サブスペックが従う。
MAIN_SPEC §3（アーキテクチャ）と §8（Android を見据えたコア/アダプタ分離）を、C++ の DDD ライト構成として具体化する。

## 背景・目的

- MAIN_SPEC §8 が既に「**共通コア（C++）★1回だけ書く** / Windowsアダプタ(TSF) / Androidアダプタ」を要求している。これは事実上 **domain+application = コア / infrastructure = アダプタ** の分離。
- C++ には GC が無く COM（TSF）は参照カウント・コールバック駆動で侵襲的。**境界を物理的に強制しないと、業務ロジックが EditSession コールバックに溶けて**、MAIN_SPEC §6.5 が禁じる「edit session 内ベタ書き」に陥る。
- テストは「最後に揃えばよい」方針。**テスト可能な純粋ロジックを TSF/COM から隔離**しておけば、それが安く成立する。

## 設計

### レイヤ定義

| レイヤ | 役割 | 中身（例） | OS/COM/LLM 依存 |
|--|--|--|--|
| **domain** | 純粋ロジック・モデル・ポート定義 | `RomajiKanaConverter`, `TriggerPolicy`, `PreeditBuffer`, `ConversionRequest`, `ConversionQueue`, `IKanaKanjiConverter`（ポート） | **不可**（純 C++ のみ） |
| **application** | ユースケースの協調・状態機械 | `InputSession`（溜める→トリガー→変換要求→確定→手放す） | **不可** |
| **infrastructure** | 外界アダプタ | TSF/COM 実装、名前付きパイプ、zenz デーモン(llama.cpp) | **ここに全部閉じ込める** |

### 依存方向（絶対ルール）

```
        infrastructure  ──依存──▶  domain ◀──依存──  application
                                     ▲
                          application ──依存──▶ domain
        （依存は常に内向き。domain は誰にも依存しない）
```

- **domain / application は `windows.h`・`msctf.h`・llama.cpp ヘッダを #include してはならない**（0-B でビルド構成上も強制）。
- infra は domain が定義した**ポート（pure virtual class）を実装**して注入する（依存性逆転）。

### ポート（依存性逆転の接点）

| ポート（domain 定義） | 実装（infra） | 導入サブスペック |
|--|--|--|
| `IKanaKanjiConverter`（hiragana→kanji, **request_id 付き非同期IF**） | `PipeKanaKanjiConverter`（名前付きパイプ） | 3-A / 3-C |
| `ITextSink` 相当（preedit 反映・確定の抽象。命名は実装時に確定） | TSF アダプタ | 1-B / 1-C |

> Phase 1 時点ではポートは最小。core 側を「TSF を知らないまま preedit を更新できる」形にしておくのが目的。

### 所有権・寿命（C++ 固有の最重要規律）

- **COM オブジェクト（`ITf*`）は参照カウントで infra に閉じ込める**。`Microsoft::WRL::ComPtr` 等で管理。
- **core へは値（`struct` / `enum` / `std::u16string` 等）だけ渡す**。core が COM ポインタを持たない。
- core の所有は値型 / `std::unique_ptr`。生ポインタの所有を持ち回らない。

### Android を見据えた境界（今は実装しない・形だけ守る）

```
┌──────────────────────────────┐
│ yoshinani.core (C++) ★1回だけ書く │ romaji管理 / トリガー判定 / 変換ポート呼び出し / キュー
└───────────────┬──────────────┘
        ┌────────┴────────┐
        ▼                 ▼
  Windows アダプタ        Android アダプタ（将来 4-E）
  TSF (C++)              IMS(Kotlin)+キーボードUI
```

→ 今やるべきことは「**core に Windows を混ぜない**」ことだけ。Android 実装は 4-E。

### ディレクトリ構成（案）

```
src/
  core/                 # yoshinani.core (static lib) — OS非依存
    domain/             #   RomajiKanaConverter, TriggerPolicy, PreeditBuffer,
                        #   ConversionRequest, ConversionQueue, ports/IKanaKanjiConverter.h
    application/        #   InputSession
  tsf/                  # yoshinani.tsf (→ yoshinani.dll) — infrastructure / Windows
                        #   TextService, KeyEventSink, CompositionManager, registration, dllmain, *.def
  ipc/                  # yoshinani.ipc (static lib) — PipeKanaKanjiConverter（3-C で実装）
  daemon/               # yoshinanid (exe) — zenz + llama.cpp（3-B で実装）
tests/
  core/                 # yoshinani.core.tests
```

### 命名規約

MAIN_SPEC §「名称・識別子（統一）」に従う。

| 用途 | 表記 |
|--|--|
| DLL | `yoshinani.dll` |
| TIP 内部名 / CLSID 識別子 | `Yoshinani` |
| IME 一覧表示名 | よしなに |

- C++ 名前空間: `yoshinani::core::domain` / `yoshinani::core::application` / `yoshinani::tsf` / `yoshinani::ipc`。

## 受け入れ条件

- [ ] レイヤ定義・依存方向・所有権方針が本ファイルに確定して記述されている
- [ ] 0-B のビルド構成で「core が Windows ヘッダを include したらコンパイルエラー」になる方針が合意されている
- [ ] 以降のサブスペックが本ファイルのレイヤ/ポート用語で記述できる

## 影響範囲

- 新規ドキュメントのみ。コードへの直接変更なし（後続サブスペックが従う基準）。

## 依存関係（他SUBSPECとの関連）

| SUBSPEC | 関連 |
|--|--|
| 0-B ビルド構成 | レイヤ分割をターゲット分割として実体化し、依存方向をビルドで強制 |
| 0-C テスト土台 | テスト対象 = core（domain/application）に隔離する根拠 |
| 1-C / 3-A / 3-D | domain にロジックを置く先（TriggerPolicy / ポート / キュー） |
| 4-E Android | core 再利用の前提。本ファイルの境界が守られていれば移植コスト低 |

## TBD（未確定事項）

- preedit 反映のポート（core→infra）を明示インターフェースにするか、application が TSF アダプタへ直接コールバックする薄い形にするか（1-B で確定）
- `std::string`(UTF-8) と `std::u16string`(UTF-16) のどちらを core の標準文字型にするか（TSF は UTF-16、変換テーブルは UTF-8 が書きやすい。境界での変換点をどこに置くか）
- 例外を使うか、エラーは戻り値（`expected` 相当）で扱うか（MSVC C++23 `std::expected` 採用可否）
