# 1-C トリガー確定

## 概要

`。` `、` / `Shift+Tab` で preedit を**そのまま確定**（変換なし）し、`Esc` で取り消す。
「トリガー＝変換していい＝そのまま確定の合図」という MAIN_SPEC の設計核を、**判定ロジックを domain に純粋実装**して TSF アダプタから呼ぶ形で実現する。これで Step1 が完結する。

## 実装メモ（現状・2026-06-09 更新）

> 経緯: `。`/`、`/`Shift+Tab` → 一旦 `Space` のみ → **最終的に `Tab`**。
> 理由: B方式の汎用LLM変換は**空白区切りのローマ字だと精度が大幅向上**（[3-B](phase3-zenz-daemon.md) 実測）。
> Space をトリガーにすると分かち書きできないため、**トリガーを Tab に逃がし、Space を区切り文字**にした。
> トリガーキーは将来の設定UIに向け**設定として分離**済み。

| 項目 | 現状 |
|--|--|
| 確定トリガー | **Tab**（preedit 非空で確定・Tab は消費。空 Tab は通常の Tab として素通し） |
| Space | **分かち書きの区切り**（preedit に空白を足す。空 Space は素通し）。トリガーには使わせない（`LoadTriggerVKs` で VK_SPACE 除外） |
| 撤去 | `。`/`、`/`Shift+Tab` のトリガー扱い（設定で再追加可能） |
| KeyKind | `Character` / **`Trigger`** / **`Space`(区切り)** / `Backspace` / `Escape` / `Other` |
| 設定の分離 | `core/application/Settings`（OS非依存・`triggerKeys` をキー名で保持・既定 `["Tab"]`）。JSON パースも core（テスト可能） |
| 保存形式 | **JSON**（`nlohmann/json` 単一ヘッダ）。`settings.json` を DLL と同じ場所に置く（無ければ既定） |
| OS依存の橋渡し | infra `src/tsf/KeyMap.cpp`：キー名 → VK 対応（`"Tab"→VK_TAB`, `"Period"→VK_OEM_PERIOD` …） |
| 拡張のしかた | settings.json の `triggerKeys` に名前を足す → 反映。新しいキー名は KeyMap に1行足す |

`settings.json` 例:
```json
{ "triggerKeys": ["Tab", "Period"] }
```

将来（Step4 付近）: 設定UIでこの JSON を編集 → キーマップ変更。句読点を「出力に含めて確定」したい
場合は `CommitWithPunct` 系のアクションを再導入し、Trigger の種類で分岐する。

## 背景・目的

- MAIN_SPEC §5: トリガー＝確定。Enter は使わない（A 自動確定では二度押し不要）。
- キーの「意味づけ」（このキーは確定か/削除か/取消か）は **OS 非依存の純粋ロジック**。ここを domain（`TriggerPolicy`）に置くと、**ユニットテストの最初の対象**になり「最後にテスト」方針に効く。
- 確定経路を 1 箇所に集約しておくことが、§6.5 ③「投入順キューで確定」を後で差し込む足場になる。

## 設計

### domain: `TriggerPolicy`（純粋・テスト対象）

正規化済みキーイベントを受け取り、「IME が取るべきアクション」を返す純粋関数。
TSF/VK コードを直接見ない（infra が正規化して渡す）。

```cpp
// src/core/domain/TriggerPolicy.h  （概念。シグネチャは実装時に確定）
namespace yoshinani::core::domain {

enum class KeyKind {
    Character,      // 通常文字（英字など。値は別途 char/char32_t で渡す）
    Kuten,          // 。
    Touten,         // 、
    ShiftTab,       // 手動トリガー
    Backspace,
    Escape,
    Other,          // 食わない
};

enum class InputAction {
    Append,             // preedit に文字を足す
    CommitWithPunct,    // 句読点ごと確定して手放す（。、）
    Commit,             // 確定して手放す（Shift+Tab）
    DeleteLast,         // 末尾 1 文字削除
    Cancel,             // 取り消して手放す（Esc）
    PassThrough,        // 食わずにアプリへ
};

InputAction Decide(KeyKind kind, bool preeditEmpty);

}
```

### 判定表（受け入れ＝この表）

| KeyKind | preedit 空 | preedit 非空 |
|--|--|--|
| `Character` | Append | Append |
| `Kuten` / `Touten`（。、） | PassThrough（※TBD: そのまま句読点を流す） | CommitWithPunct |
| `ShiftTab` | PassThrough | Commit |
| `Backspace` | PassThrough | DeleteLast |
| `Escape` | PassThrough | Cancel |
| `Other` | PassThrough | PassThrough |

> 句読点の確定では「。/、」も preedit に含めて一緒に確定して流す（MAIN_SPEC §5）。

### infra: 正規化と実行（TSF アダプタ）

```
OnKeyDown(wParam=VK, ...)
  ├─ infra: VK + 修飾キー(GetKeyState) → KeyKind に正規化
  │         （VK_OEM_PERIOD/IME 入力文字→。、 / Shift+VK_TAB→ShiftTab / VK_BACK / VK_ESCAPE / 英字→Character）
  ├─ TriggerPolicy::Decide(kind, preeditEmpty) → InputAction
  └─ action に応じて edit session 内で:
        Append          → CompositionManager に文字追記（1-B）
        DeleteLast      → 末尾削除（1-B）
        CommitWithPunct → 句読点を range 末尾に追記 → EndComposition（確定）
        Commit          → EndComposition（確定）
        Cancel          → range クリア → EndComposition（取消）
        PassThrough     → *pfEaten=FALSE
```

### 確定経路の集約（§6.5 ③ の足場・v1 は素通り）

- 確定は `CommitCurrent()` のような **単一の関数**を必ず経由させる（複数箇所から直接 EndComposition しない）。
- v1 は「即確定（1件）」だが、将来ここに**投入順キュー**を挟めるよう、確定要求を 1 関数に集約しておく。
- Step1 では変換が無いので「確定する中身 = 溜めた英字（+句読点）」そのまま。

### レイヤ整理（0-A 準拠）

| 要素 | レイヤ |
|--|--|
| `TriggerPolicy::Decide`（意味判定） | domain |
| `InputSession.CommitCurrent()` 等の協調 | application |
| VK 正規化・EndComposition・range 操作 | infrastructure（TSF） |

## 拡張要件: Enter の挙動（2026-06-09 追加 — Google 日本語入力準拠）

> 要望: Enter を Google 日本語入力（Mozc）と同じ挙動にしたい。

- **composition 中**: Enter は **今の preedit を「打ったそのまま」生確定**（**変換しない**）し、**改行は出さない**。
  - Google IME 準拠の解釈: Enter は「未確定文字列の確定」。確定後（preedit 空）の Enter は通常の改行。
  - B方式の「変換して確定」は **Tab**。Enter は「変換せず今の preedit をそのまま確定」＝ローマ字/記号のまま入れたい時の確定手段。
- **composition なし（preedit 空）**: Enter は**食わず素通し**＝アプリ標準の改行。
- 実装: `KeyKind` に `Enter` を追加し、`Decide`:
  - `Enter` × preedit 非空 → 新アクション `CommitRaw`（変換なし生確定・EndComposition）
  - `Enter` × preedit 空 → `PassThrough`
- Tab（変換確定）も Enter（生確定）も **`CommitCurrent()` 集約口**を通す（複数箇所から EndComposition しない）。

### 受け入れ条件（追加）
- [ ] preedit 非空で Enter → 下線が消え、打った文字が**変換されずそのまま**確定（改行なし）
- [ ] preedit 空で Enter → 通常の改行
- [ ] Tab（変換確定）と Enter（生確定）が共存して期待どおり動く

### TBD（Enter）→ 確定（2026-06-10）
- **生確定で確定**（ユーザー確認済み）。`KeyKind::Enter` × preedit 非空 → `InputAction::CommitRaw`（変換なし）。
  Enter は生確定専用とし、settings.json の triggerKeys に "Enter" を書いても無視する
  （`LoadTriggerVKs` で VK_RETURN を除外。Space の区切り専用ガードと同じ扱い）。

## 受け入れ条件（MAIN_SPEC §7 より）

- [ ] トリガー（`。`/`、`/`Shift+Tab`）で下線が消え、打った英字が**そのまま確定**してアプリに残る
- [ ] `。`/`、` は句読点も一緒に確定して流れる
- [ ] `Esc` で preedit が消える（**確定されない**）
- [ ] preedit が空のとき各キーがアプリへ素通りする（CTBD の挙動に従う）
- [ ] `TriggerPolicy::Decide` の判定表が**ユニットテストで網羅**されている（0-C の枠を使用）
- [ ] IME OFF / 別 IME 切替 → 復帰で落ちない

## 影響範囲

### 新規ファイル
- `src/core/domain/TriggerPolicy.{h,cpp}`
- `tests/core/test_trigger_policy.cpp`（判定表テスト）

### 修正
- `src/tsf/KeyEventSink.cpp`（VK→KeyKind 正規化、Decide 呼び出し、action 実行）
- `src/tsf/CompositionManager.{h,cpp}`（`CommitCurrent()` 経路の集約・確定/取消）
- `src/core/application/InputSession.{h,cpp}`（確定要求の集約口）

## 依存関係（他SUBSPECとの関連）

| SUBSPEC | 関連 |
|--|--|
| 1-B | composition の追記/削除/終了を本ファイルのトリガーで駆動 |
| 0-C | `TriggerPolicy` のユニットテストが最初の実テスト |
| 2-A/2-B | 確定する中身が「英字」→「かな」に変わる（経路は据え置き） |
| 3-D/3-E | `CommitCurrent()` の集約口に投入順キュー・zenz 変換結果を差し込む |

## TBD（未確定事項）

- preedit が空のときの「。/、」の挙動（句読点をそのまま流す＝PassThrough か、IME として何かするか）
- `Shift+Tab` を `ITfKeystrokeMgr::PreserveKey`（予約キー）で取るか、`OnKeyDown` 内で Shift 状態を見て判定するか（アプリの Tab と競合しないか要検証）
- ローマ字入力中に IME 経由で全角「。、」が来るケースと、半角キー直接入力のケースの正規化差異
- 確定後にカーソル/フォーカスが期待通りアプリ側に戻るか（A 自動確定＝手放すの検証）
