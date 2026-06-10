# 1-B preedit ライフサイクル

## 概要

英字キー押下で **下線付きの未確定文字列（preedit / composition）** を表示し、編集（追記・削除）できるようにする。
1-A で登録した sink の中身を実装する。MAIN_SPEC Step1 の核。変換はまだしない（ローマ字をそのまま preedit に入れる）。

## 背景・目的

- preedit ＝ IME が所有する未確定領域。MAIN_SPEC のコア「①溜める」を実体化する部分。
- ここで composition の開始・追記・終了の作法（edit session / range 操作）を固めれば、2 以降は「入れる中身」を差し替えるだけになる。
- §6.5 B方式（将来 async で未確定保持）の足場でもある。preedit を IME 所有のまま保てる構造にしておく。

## 設計

### 実装インターフェース

| インターフェース | 役割 |
|--|--|
| `ITfKeyEventSink` | キー捕捉。`OnTestKeyDown` / `OnKeyDown`（`OnTestKeyUp`/`OnKeyUp` は最小実装） |
| `ITfThreadMgrEventSink` | フォーカス/コンテキスト変化追跡（`OnSetFocus` / `OnPushContext` / `OnPopContext`） |
| `ITfCompositionSink` | `OnCompositionTerminated`（アプリ側終了で preedit を後始末） |
| `ITfEditSession` | ドキュメントロック下でテキスト編集（`DoEditSession`） |
| （後回し可）`ITfDisplayAttributeProvider` | preedit の下線スタイル明示。**1-B では既定描画に任せ未実装でよい** |

### preedit の流れ

```
英字キー押下
  OnTestKeyDown → 「自分が食うキーか」を判定して *pfEaten=TRUE（消費宣言）
  OnKeyDown     → ITfContext::RequestEditSession(TF_ES_READWRITE [|TF_ES_SYNC])
        │
        ▼ EditSession::DoEditSession 内（ドキュメントロック取得済み）
  composition 未開始なら:
     ITfContext → QI(ITfContextComposition)::StartComposition で composition 開始
        （開始位置の range を作って ITfCompositionSink を渡す）
  composition の range に ITfRange::SetText で英字 1 文字を追記
  → 下線付き preedit としてアプリに表示される
```

- **キー消費の規律**: preedit 中の英字・Backspace・トリガー等「自分が扱うキー」だけ `*pfEaten=TRUE`。それ以外（矢印キー等）は食わずにアプリへ通す。Step1 では「英字 + Backspace + Esc + トリガー（1-C）」のみ消費。
- **Backspace**: preedit 末尾 1 文字を range 操作で削除。空になったら composition 終了。
- **Esc**: range をクリアして composition 終了（取り消し）。※ 確定/取り消しのトリガー判定そのものは 1-C に委譲。

### core との関係（0-A 準拠）

- application に `InputSession`（溜め状態）を置き、infra（TSF アダプタ）は「どの文字を追記/削除すべきか」を `InputSession` に問い合わせて、その結果を range 操作に反映する薄い仲介に徹する。
- Step1 では `InputSession` の中身は「受け取った英字をそのまま溜める」だけ。**TSF アダプタに業務分岐を書かない**（§6.5「edit session 内ベタ書き禁止」を最初から守る）。
- core へは値（文字・キー種別 enum）だけ渡す。`ITf*` を core に渡さない。

### EditSession 同期/非同期

- v1 は同期前提でよいが、`RequestEditSession` の戻り（ロック付与可否）を必ずハンドリングする。
- 将来の async（§6.5）に備え、「キーを食う判断」と「実際の編集（edit session 内）」を関数として分離しておく。

## 拡張要件（2026-06-09 追加 — B方式の入力リッチ化）

> 経緯: B方式（romaji→Gemma 直渡し）では preedit に**ローマ字＋記号＋空白**をそのまま溜め、
> Tab で丸ごと変換する。実機確認で「英小文字しか入らない／大文字・記号が素通し」と判明したことへの対応（HANDOFF §2）。

### R1: Shift で大文字入力
- 現状 `Classify` は VK `'A'-'Z'` を**常に小文字**へ写像し Shift を無視している。
- 対応: 打鍵文字を **`ToUnicodeEx`（キーボード状態・レイアウト・Shift/CapsLock 反映）** で取得して preedit に入れる。VK→小文字の決め打ちをやめる。
- 目的: `OpenAI` `Class` 等、Latin 正規化が要る語の**大小を保持**して Gemma に渡す。

### R2: 記号も preedit に入力
- `" ' [ ] ( ) , . - ! ? : ; / @ #` 等の記号も composition への追記対象にする
  （例: `"super nice"` / `'tyousugoi'` / `[konna kanji nimo]` / `wa----sugoi!!` / `konna nyuuryoku mo!?`）。
- 実装方針は R1 と同じく**打鍵文字を `ToUnicodeEx` で取得**して追記（記号ごとの VK 個別マッピングが不要になる）。
- 「食う／素通しする」線引き:
  - **印字可能文字は preedit が空でも食って composition を開始する**（Google IME 準拠・2026-06-10 確定）。
    `"super nice"` の先頭 `"` から溜められる。コーディング時の邪魔さは 1-D 直接入力モードで回避する。
  - 制御キー（矢印・Home/End・ファンクション・Ctrl/Alt 併用）は従来どおり素通し。
  - **入力モードが「直接入力」のとき（1-D）は全て素通し**。
- 変換時はこの記号込みの文字列をそのまま Gemma へ渡す（Latin 正規化方針と整合・HANDOFF §4）。

### 受け入れ条件（追加）
- [ ] `Shift+o` 等で大文字 `O` が preedit に入る（Chromium 系で目視）
- [ ] `" ' [ ] - ! ?` 等の記号が preedit に入り、Tab で変換対象に含まれる
- [ ] 矢印・Ctrl 併用などの制御キーは従来どおり素通し

## 受け入れ条件（MAIN_SPEC §7 より）

- [ ] メモ帳で「よしなに」に切替後、英字キーで**下線付き preedit** が表示される
- [ ] 連続入力で preedit が伸びる（複数文字が溜まる）
- [ ] `Backspace` で preedit 末尾が 1 文字消える。空で composition が終了する
- [ ] フォーカス移動・別アプリ切替で preedit が破綻しない（`OnSetFocus` / `OnCompositionTerminated` で後始末）
- [ ] 矢印キーなど非対象キーは食わずアプリに通る

## 影響範囲

### 新規ファイル（`src/tsf/`）
- `KeyEventSink.{h,cpp}`
- `ThreadMgrEventSink.{h,cpp}`
- `CompositionManager.{h,cpp}`（composition 開始/追記/終了、`ITfCompositionSink`）
- `EditSession.{h,cpp}`（`ITfEditSession` 実装。ラムダ/コールバックで編集処理を受ける形）

### 新規ファイル（`src/core/application/`）
- `InputSession.{h,cpp}`（溜め状態。Step1 は最小）

### 修正
- `TextService.cpp`（1-A の sink 登録を実装に結線）

## 依存関係（他SUBSPECとの関連）

| SUBSPEC | 関連 |
|--|--|
| 1-A | sink の Advise・Activate 基盤 |
| 1-C | トリガー（確定/取消）の判定と EndComposition を本ファイルの composition 上で行う |
| 2-B | preedit に入れる中身を「英字そのまま」→「かな」に差し替える結線点 |
| 4-B | `ITfDisplayAttributeProvider` で色分け（ここでは未実装） |

## TBD（未確定事項）

- edit session を同期（TF_ES_SYNC）で要求するか非同期で要求するか（ホストにより同期が拒否される場合の扱い）
- composition の range 末尾追記の実装詳細（`ITfRange::SetText` のクローン range 管理）
- `InputSession`（core）と TSF アダプタ間のインターフェース形（明示ポート vs コールバック）— 0-A の TBD と同期
- preedit が空のときのトリガー入力（何も溜まっていない状態で「。」が来た場合）の扱い → 1-C で確定
