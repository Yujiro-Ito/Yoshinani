# よしなに (Yoshinani) — 仕様書 (SPEC.md) v2.1

> **よしなに**「よしなにやっといて」を、本当にやるIME。ローマ字を適当に打てば、LLMが察して日本語にする。

> 配置先: `.spec/yoshinani/active/SPEC.md`
> 運用: AGENTS.md / MEMORY.md / HANDOFF.md と同ワークフロー。PR番号は本ファイル内に追記。
> v2変更: 設計を大幅スリム化。**A確定（一方通行）/ トリガー＝確定 / 修正は既存IMEに外注**。
> Mozc内蔵・非同期変換・確定後の範囲記憶・修正モードを **撤廃**。

---

## 0. ゴール（一行）

アルファベット（ローマ字）で打ち続け、句読点 or `Shift+Tab` で **zenz が文脈を理解して一括変換 → 自動確定** してアプリへ流す自作Windows IME。誤変換は直さない（直したい時だけ `半角/全角` で既存IMEに切替）。

### 名称・識別子（統一）
| 用途 | 表記 |
|--|--|
| プロダクト名 | **よしなに** |
| 英字表記 | **Yoshinani** |
| リポジトリ名 | `yoshinani` |
| TIP内部名 / CLSID識別子 | `Yoshinani` |
| DLL名 | `yoshinani.dll` |
| IME一覧の表示名 | よしなに |


```
ローマ字を打つ ──→ preeditに溜まる（変換しない・自由に打てる）
       │  「。」「、」or Shift+Tab
       ▼
zenzで一括変換 ──→ そのまま自動確定してアプリへ流す（Enter不要・手放す）
       │
      終わり（戻らない・置換しない）
       │ 直したい時だけ
       ▼ 半角/全角キー
   既存 Google日本語入力（そのまま使う）
```

---

## 1. 確定した設計判断（ブレない軸）

| 論点 | 決定 | 不採用にしたもの |
|--|--|--|
| 対応範囲 | **全アプリ → IME(TSF)を貫く** | エディタプラグイン方式（LazyJP型・エディタ限定）|
| 変換単位 | 句読点 / `Shift+Tab` トリガーで一括 | 打鍵ごとの逐次変換 |
| 確定方式 | **A: トリガー＝変換＋自動確定＋手放す**（一方通行）| 確定後に戻って置換するB方式 |
| 修正 | しない。`半角/全角` で既存IMEに切替えて打ち直す | Mozc文節再変換の内蔵 |
| 同期/非同期 | **v1は同期**（トリガー時のみ・zenz 90Mは速い）。将来のasync化は §6.5 の継ぎ目で対応 | v1での非同期実装 |
| preedit保持 | 溜め中のみ保持、確定で即手放す | 長時間保持・伸ばし続け |

---

## 2. なぜこんなにシンプルになったか（撤廃リスト）

割り切りの連鎖で、仕様がどんどん消えた。減ったぶん事故らない。

| 撤廃項目 | 消えた理由 |
|--|--|
| 確定後の範囲記憶（`ITfRange`保持）| A一方通行＝戻らないので不要 |
| 非同期置換・衝突回避 | 同上。置換しないから衝突も無い |
| Mozc文節再変換の内蔵 | 修正を既存IMEに外注したので不要 |
| バックグラウンド変換 | v1では不要（トリガー方式で「変換待ち」が構造的に消えた）。将来モデル大型化時は §6.5 の継ぎ目でasync化 |
| Mozcフォーク（変換エンジンとして）| romaji→kana（決定的）+ zenz で変換が完結 |

→ 残るコアは **①溜める ②トリガーで変換 ③自動確定して手放す** の3つだけ。

---

## 3. アーキテクチャ

```
┌──────────────┐   キー入力    ┌────────────────────────────┐
│ 各アプリ      │ ◀───TSF────▶ │ TIP DLL（インプロセス／C++）   │
│ (メモ帳等)    │   preedit表示 │  ・キー捕捉 ITfKeyEventSink    │
└──────────────┘   自動確定    │  ・未確定文字列 ITfComposition │
                               │  ・romaji→hiragana（決定的）    │
                               └──────────────┬─────────────┘
                                              │ IPC（名前付きパイプ）
                                              ▼
                               ┌────────────────────────────┐
                               │ zenzデーモン（別プロセス）       │
                               │   llama.cpp + zenz (GGUF)      │ hiragana→kanji
                               └────────────────────────────┘
```

変換パイプライン（当初設計・zenz経由）:
```
romaji ──(決定的変換・モデル不要)──▶ hiragana ──(zenz / llama.cpp)──▶ kanji
```

> 更新(2026-06-09): 変換コアを **B方式（ローマ字を汎用LLMに直渡し）** に変更。
> 先行例 LazyJP（romaji→汎用LLM で英語混じり・文脈ごと一括変換）の方式を採り、
> それを **TSF で全アプリ化＋できればローカルで** 実現する（LazyJP はクラウド/エディタ限定）。
> **まず Gemma 4 E2B（GGUF・Apache 2.0・llama.cpp 初日対応）から試す**（不足なら E4B）。zenz は「全部日本語の速い道／fallback」として残す。
>
> 新・変換パイプライン（主経路）:
> ```
> romaji ──(汎用LLM ＝ Gemma 等 / llama.cpp)──▶ 日本語（漢字・かな・英数 混在）
> ```
> - `romaji→kana`（決定的）は主経路では不要に**格下げ**（zenz fallback・かな表示のときだけ）。
> - preedit は**生ローマ字表示**でよい（LazyJP 方式）。トリガー(Space)で一括変換。
> - 変換モデルは差し替え可能（Gemma local / zenz(かな経由) / 将来 cloud）。**勝者は実測で選ぶ**。

**重要**: LLMをTIPに同居させない。TIPは全アプリのプロセス内に毎回ロードされるため、モデルを載せると全アプリが重くなる。→ zenzは別プロセス常駐、TIPは名前付きパイプで結果文字列だけ受け取る薄いクライアント。

---

## 4. 技術スタック

> 更新(2026-06-09): 変換コアを **B方式（ローマ字→汎用LLM 直渡し）** に変更（§3 更新参照）。
> まず **ローカル Gemma 4 E2B（GGUF・Apache 2.0）** から試す。zenz は速い道/fallback として残置。

| 層 | 採用 | 備考 |
|--|--|--|
| OS連携 | **TSF** | IMM32ではなくTSF |
| 実装言語 | **C++** | TSF公式サンプル・Mozc参考実装と同一 |
| **変換コア（主）** | **汎用LLM（Gemma 4 E4B-it-qat・GGUF・Apache 2.0・思考オフ）** にローマ字直渡し | B方式。**実測で E4B 採用**（E2B は英語混在/空白なしで崩れた）。~1s・GPU常駐。詳細は [3-B](sub-specs/phase3-zenz-daemon.md) |
| 変換コア（fallback/速い道） | **zenz (Miwa-Keita/zenz, GGUF)** | 90M・かな専用・CC-BY-SA 4.0。全部日本語のとき高速。要 `romaji→kana` 前段 |
| romaji→kana | 自前の決定的変換（**任意・格下げ**） | zenz fallback／かな表示のときだけ。テーブル＋促音/撥音ルール |
| LLM実行基盤 | **llama.cpp** | Gemma / zenz の GGUF を実行。Androidでも動く |
| 学習・体感用 | Ollama | 「ローカルLLM実行」の練習用。本番には使わない |
| TIP⇔デーモン | **名前付きパイプ** | プロセス分離のIPC |

参考実装に **LazyJP (raspy135/lazyjp-vscode)** を追加 — romaji→汎用LLM 直変換・トリガー一括・
背景書き換え・継続モード（文脈チェイン）・`/rule` インラインルールの設計を取り込む。

参考実装:
- **Mozc `src/win32/tip/`** … TSF text serviceの本物のC++実装。**TSF層の書き方のお手本**として読む（変換エンジンとしては使わない）
- **Microsoft TSF SampleIME** … 最小TIPの参考
- **AzooKeyKanaKanjiConverter** … zenz/投機的デコーディングの実装例（将来の速度最適化の参考）

---

## 5. トリガー／キー仕様

> 更新(2026-06-09): **確定トリガーは `Tab`（既定）**。**`Space` は分かち書きの区切り**に使う。
> 理由: B方式の汎用LLM変換は**空白区切りのローマ字だと精度が大幅に上がる**（実測）。
> Space をトリガーにすると区切りが打てないため、トリガーを Tab に逃がした。
> トリガーキーは設定（`settings.json` の `triggerKeys`）で変更・追加できる（[[phase1-trigger-commit]]）。

| キー | 役割 |
|--|--|
| `Tab`（既定トリガー） | 変換 + 自動確定（トリガーキー自体は消費）。preedit が空なら通常の Tab として素通し |
| `Space` | **分かち書きの区切り**（preedit に空白を足す）。preedit が空なら通常の空白として素通し |
| `Backspace` | preedit末尾を削除 |
| `Esc` | preedit取り消し |
| ~~`Enter`~~ | **使わない**（A自動確定では不要） |
| （設定で追加可）`。` `、` 等 | キーマップ設定で確定トリガーに追加できる |

```
打鍵中（空白で分かち書き）──→ Tab（既定トリガー）──→ 変換+自動確定+手放す
   例: kyou ha openai de class wo tukutta              │
       → 今日はOpenAIでclassを作った            すぐ次の入力へ（止まらない）
```

キーマップは **JSON 設定**（`triggerKeys` をキー名で保持）で持ち、将来は設定画面から
変更できるようにする（Google日本語入力の方式）。設定データは OS 非依存の core に置く。

設計の核: **トリガー＝「変換していい」という意思表示＝そのまま確定の合図**。だからEnterで二度押しさせない。

---

## 6. 段階実装ロードマップ

| Step | 内容 | 成果（証明できること） |
|--|--|--|
| **1** | TSF最小TIP。ローマ字をそのままpreedit→トリガーで確定（変換なし）| OS認識・キー捕捉・preedit表示・確定＝最大の壁を突破 |
| **2** | `romaji→hiragana` の決定的変換を追加（モデル不要）| これだけで「ローマ字かな入力」が成立 |
| **3** | zenzデーモン(llama.cpp)接続。`hiragana→kanji`、A自動確定 | 文脈変換が動く＝**プロダクト完成** |
| 4（任意）| 磨き: async化（§6.5）＋変換中segmentの色分け表示／固有名詞辞書／文体プロンプト／速度最適化（古典エンジン＋投機的デコード）／Android対応 | 体験の質向上 |

★ **Step1〜3でプロダクトとして完結する。** Step4は全部「あったら嬉しい」枠。

---

## 6.5 拡張性のための継ぎ目（v1では実装しない・形だけ仕込む）

### 背景：将来のasync化に備える

v1は同期で作る。だが将来 **zenzをより大きいモデルに替えると変換が遅くなり**、確定→次の入力→確定…で「変換待ちの行列」が発生し得る。
ここで効くのが **async化**。ただし重要なのは方式の選択：

| | A: 確定→後で置換 | **B: 変換まで未確定のまま保持** |
|--|--|--|
| 仕組み | romajiを確定して流す→結果が来たら範囲を探して置換 | 変換中もpreedit（IME所有）のまま保持→済んだら投入順に確定 |
| 領域の持ち主 | アプリ | **IME自身** |
| 他アプリが触るリスク | あり（→破綻） | なし（→御せる） |
| 採用 | ✗ | **◎** |

```
B方式（採用）:
  確定済み | [A:変換中] [B:変換中] [C:打鍵中]   ← この区間は全部まだpreedit
              ▲ IMEが丸ごと所有 → Aの結果でA部分だけ差し替えても破綻しない
              ▲ 確定は投入順。Aが済むまでBは確定しない
  = real IMEが長文を複数文節で持つのと同じ構造。行列しても御せる。
```

> 注意: これは「同時に複数preeditを打つ」話ではない。preeditの“見た目”は実質1連結。
> 内部で「変換待ちが複数キューに並ぶ」だけ。同時入力ではなく **変換キューの行列** を捌く話。

### v1で守るべき3つの継ぎ目（コストほぼゼロ・後悔回避）

| 継ぎ目 | v1での実装 | 後でasync化する時 |
|--|--|--|
| **① 変換呼び出しを `request_id` 付き非同期IFにする** | 投げて即待つ（実質同期）。ただし形は非同期コールバック | 「待たない」に変えるだけ |
| **② 変換待ちを“リスト”で持つ** `{id, range, reading, state}` | 最大1件。器はN件入る形にしておく | `MAX=1` を外すだけ |
| **③ 確定は“投入順キュー”経由で行う** | 1件なので素通り。順序保証の口だけ開けておく | キューが本来の役割を果たす |

### v1でやってはいけないこと（やると後で全書き直し）
```
✗ edit session内で「同期ブロック前提」のベタ書き
✗ 「変換結果を即確定する前提」のベタ書き
   → どちらもasync化＝全面書き直しになる
```

### 結論
**v1は同期・1件で素直に作る。ただし①②③の“形”だけ最初から入れる。**
これで「シンプルに完成」と「将来のasync拡張」が両立する。async + 色分け表示の実装はStep4（磨き枠）。

---

## 7. Step 1 詳細仕様（← 今ここ）

### 目的
変換は一切しない。**ローマ字をそのまま未確定文字列にして、トリガーで確定するだけ**の最小TIPをビルドし、Windowsに認識させる。これが動けば以降は「preeditに入れる中身」を差し替えるだけ。

### 実装するインターフェース（最小セット）
| インターフェース | 役割 |
|--|--|
| `ITfTextInputProcessorEx` | エントリ。`Activate` / `Deactivate` |
| `ITfThreadMgrEventSink` | フォーカス/コンテキスト変化の追跡 |
| `ITfKeyEventSink` | キー捕捉。`ITfKeystrokeMgr::AdviseKeyEventSink` でadvise |
| `ITfCompositionSink` | `OnCompositionTerminated` |
| `ITfEditSession` | ドキュメントロック下でテキスト編集 |
| （後回し可）`ITfDisplayAttributeProvider` | preeditの下線スタイル |

### preeditの流れ
```
英字キー押下
   │  ITfContext::RequestEditSession(TF_ES_READWRITE)
   ▼
EditSession内: composition未開始なら StartComposition / ITfRange::SetText で1文字追記
   ▼
トリガー(。、/ Shift+Tab) → ITfComposition::EndComposition（中身をそのまま確定）
Esc → 範囲クリアして EndComposition（取り消し）
```

### COM登録
- 標準COM: `DllGetClassObject` / `DllRegisterServer` / `DllUnregisterServer` / クラスファクトリ / CLSID自己登録
- TSF登録: `ITfInputProcessorProfiles::Register` + `AddLanguageProfile`(ja-JP)、`ITfCategoryMgr::RegisterCategory` に `GUID_TFCAT_TIP_KEYBOARD`
- 確認: `regsvr32 yoshinani.dll` → 設定 > 言語 > IME 一覧に「よしなに」が出る

### 受け入れ条件
- [ ] `regsvr32` で登録でき、IME一覧に表示される
- [ ] メモ帳で自作IMEに切替後、英字キーで **下線付きpreedit** が出る
- [ ] トリガー（`.`/Shift+Tab）で下線が消え、打った英字がそのまま確定する
- [ ] `Esc` でpreeditが消える（確定されない）
- [ ] IME OFF/別IME切替→復帰で落ちない

### スコープ外（やらない）
- 変換（Step2以降）／ zenzデーモン・IPC（Step3）／ 候補ウィンドウ

---

## 8. 将来: Android対応（設計だけ今決めておく）

実装は後回しでよいが、**「コアとアダプタの分離」だけは今のC++設計で守る**。これで将来の作り直しを回避。

```
┌─────────────────────────────┐
│ 共通コア（C++）★1回だけ書く    │ romaji管理 / トリガー判定 / zenz呼び出し
└──────────────┬──────────────┘
        ┌───────┴───────┐
        ▼                ▼
  Windowsアダプタ      Androidアダプタ
  TSF (C++)           IMS(Kotlin)+キーボードUI（Androidの新規コスト）
```
zenz+llama.cppはAndroidでも動作実績あり（Sumire等）→ コア移植リスクは低い。

---

## 9. 参照リンク
- Mozc: https://github.com/google/mozc  （TSF実装 `src/win32/tip/`）
- zenz: https://huggingface.co/Miwa-Keita/zenz-v2.5-medium / https://huggingface.co/Miwa-Keita/zenz-v1
- AzooKeyKanaKanjiConverter: https://github.com/azooKey/AzooKeyKanaKanjiConverter
- llama.cpp: https://github.com/ggml-org/llama.cpp
- Microsoft TSF SampleIME: https://github.com/microsoft/Windows-classic-samples

---

## 10. 進捗ログ（PR番号はここに追記）
- 2026-06-09: v1作成。
- 2026-06-09: v2。A確定 / トリガー＝確定 / 修正は既存IMEに外注 に確定。Mozc内蔵・確定後の範囲記憶・修正モードを撤廃。Step構成を romaji→kana / kana→kanji の2段に再編。
- 2026-06-09: v2.1。§6.5「拡張性のための継ぎ目」を追加。v1は同期のまま、将来のasync化（B方式＝未確定保持・投入順確定）に備え、①request_id付き非同期IF ②変換待ちリスト ③投入順キュー の3継ぎ目を形だけ仕込む方針を明記。
- 2026-06-09: プロジェクト名を「よしなに (Yoshinani)」に決定。タイトル・DLL名(`yoshinani.dll`)・配置先(`.spec/yoshinani/`)・識別子を統一。キャッチコピー追加。
- 2026-06-09: 実装着手。Phase 0（CMake足場・ライトDDD・自己完結ビルド/ctestループ）→ Phase 1（最小TIP：登録・preedit・確定）を実装し実機動作確認。サブスペックを `.spec/sub-specs/` に整備。
- 2026-06-09: 確定トリガーを `Space` のみに変更し、トリガーキーを設定（JSON `settings.json`）として分離（§5 更新）。`。`/`、`/`Shift+Tab` は設定で再追加できる位置づけに。
- 2026-06-09: 変換コアを **B方式（ローマ字→汎用LLM 直渡し）** に方針転換（§3/§4 更新）。先行例 LazyJP（romaji→汎用LLM・トリガー一括・背景書き換え・継続モード・`/rule`）を参考に。まず **ローカル Gemma 4 E2B-it（GGUF・Apache 2.0）** で品質を実測する方針。`romaji→kana`(2-A) は zenz fallback 用に格下げ、preedit は生ローマ字表示。
