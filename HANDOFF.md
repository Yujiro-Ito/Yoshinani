# HANDOFF — よしなに (Yoshinani) セッション引き継ぎ

> 最終更新: 2026-06-10。次セッションはまず本ファイル → [.spec/MAIN_SPEC.md](.spec/MAIN_SPEC.md) → [.spec/sub-specs/README.md](.spec/sub-specs/README.md) の順で読むと早い。

## 0. これは何
ローマ字を適当に打ち、**Tab で一括変換＋自動確定**する自作 Windows IME（TSF / C++）。
変換は**ローカル汎用LLM（Gemma 4）にローマ字を直渡し**して日本語にする（B方式）。
詳細仕様は MAIN_SPEC。リポジトリ: GitHub `Yujiro-Ito/Yoshinani`（`main`、push 済み。最新ハッシュは `git log --oneline` 参照）。

---

## 1. 守るべきルール（重要）
- **コミット前コードレビュー必須**（[CLAUDE.md](CLAUDE.md)）: コード変更を含むコミットの前に `feature-dev:code-reviewer`（or `/code-review`）を実行→重大指摘を修正→ビルド&ctest緑→コミット。`.md` のみの変更はレビュー不要。
- グローバル規約（`~/.claude/CLAUDE.md`）: **常に日本語で応答** / **Bash で `cd` 禁止**（作業ディレクトリは常にプロジェクトルート）/ コミット・push は**頼まれた時だけ**。
- 大きな判断・実装完了時は「デイリーノートへ記録しますか？」と提案（ユーザーが個人作業と言ったものは提案不要）。

---

## 2. 現在地（実装済み・検証済み）
- **Phase 0（足場）完了**: ライト DDD レイヤを CMake ターゲットに分割（`yoshinani.core` / `.ipc` / `.tsf`=DLL / tests）。`scripts/build.ps1` で **vcvars 読込→cmake→ninja→ctest を1コマンド**（自己完結ループ）。
- **Phase 1（最小TIP）完了・実機確認済み（Chromium/Electron 系のみ）**: COM登録＋TSFプロファイル(ja-JP)登録、preedit 表示、Tab トリガー確定、Space 区切り、Esc 取消。**ただし標準アプリ（Win11 新メモ帳等）では TIP が未 engage（§2 既知課題）**。
- **設定分離**: トリガーキーを `settings.json`(JSON, nlohmann) に分離（`core/application/Settings` + `src/tsf/KeyMap`）。
- **トリガー = Tab、Space = 分かち書きの区切り**（最新コミット 83ebba7）。理由は §4 参照。
- **入力リッチ化 R1/R2/Enter 完了・実機確認済み（2026-06-10・Chromium 系で全項目 OK）**:
  - R1: Shift で大文字 / R2: 記号・数字も preedit に入る（空でも composition 開始＝Google IME 準拠）。実装は `KeyMap::VkToChar`（`ToUnicodeEx`・フラグ 0x4 でカーネル状態非破壊・UWP の lParam=0 は `MapVirtualKeyEx` フォールバック）。
  - Enter: preedit 非空→**生確定**（`KeyKind::Enter`→`InputAction::CommitRaw`・変換なし・改行なし）/ 空→素通し（改行）。**Enter は triggerKeys に書いても無視**（`LoadTriggerVKs` で VK_RETURN 除外・Space と同じガード）。
  - 1-D（モード切替）は未実装だが TBD は確定済み: 切替キー=半角/全角・既定=変換モード（spec [1-D](.spec/sub-specs/phase1-input-mode.md)）。
- **変換方針 決定済み**: B方式（romaji→汎用LLM直渡し）。モデル = **Gemma 4 E4B-it-qat**（ローカル・GGUF・Apache 2.0）、**思考オフ(think=false)必須**。

### 検証結果（2026-06-09）と既知課題
- **Tab/Space ランタイム確認を実施**。結果は**アプリ依存**:
  - **Chromium/Electron 系（Claude アプリ・VSCode・Chrome 等）= 完全動作**: 下線付き preedit / Space が区切りとして preedit に入る / Tab 確定 / Esc 取消、すべて目視 OK。**Phase 3 はこの環境で開発・検証できる**。
  - **Win11 新メモ帳など標準アプリ = TIP が入力に engage しない**: キーが全く eaten されず素通し（直接入力・下線なし・Tab/Esc 無反応）。DLL 自体はメモ帳にもロードされる（列挙目的）が `OnKeyDown` が発火していない。
- **🔴 既知課題: 標準 Win32/パッケージアプリでの TIP 非 engage**。`GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT` + `SYSTRAYSUPPORT` をカテゴリ登録に追加したが**それだけでは未解決**（必要だが不十分）。残る有力候補: キーボード open/close コンパートメント未設定 / フォーカス・表示属性まわりの実装不足。次に着手するなら **TIP に軽量ログ（Activate/OnKeyDown の発火をアプリ別に記録）で局所化**が確実。**ユーザー判断で当面保留 → Phase 3（B 方式の本丸）を優先**。
- **DLL ロックは広範化**: register により `yoshinani.dll` が explorer.exe / Chrome / Unity / Claude などに常駐ロード。元パス `build/ninja-debug/.../yoshinani.dll` は reboot 級でないと再リンク不可。→ 当面は **`build.ps1 -BuildDir build/ninja-fix` 等の別ディレクトリ出力で回避**（実施済み）。恒久対策案（ビルド出力とは別ファイルを登録して「ビルド出力＝誰もロードしない」分離）は保留中。「毎回ユニーク名コピー」案は増え続けるため不採用で確定。
- **クラウド変換 結線済み（2026-06-10・実機 OK）**: `OpenAiKanaKanjiConverter`（WinHTTP HTTPS→`/v1/chat/completions`）を追加し、settings.json の `converter`（backend/model/reasoningEffort）で選択可能に（`TextService` の直接 new 残債①解消）。**既定 = openai / gpt-5.4-mini / medium**。キーは `%USERPROFILE%\.yoshinani\openai.key`（配置済み・コミット禁止）。キー不在/失敗は生ローマ字フォールバック。**空白なしローマ字が実機で変換できることを確認**（長文・難文も OK。誤字は「忠実変換」ルールにより音のまま残る＝仕様）。コピペ文字列は IME を経由しないので変換不可（TSF の仕様）。
- **現在 IME は登録されたまま**（クラウド変換版 `build/ninja-cloud1/src/tsf/yoshinani.dll`・2026-06-10 切替済み）。Chromium 系で **romaji→Tab→Gemma→日本語確定が実動作**。再ビルドは別 dir 必須（ロック）。不要になったら `regsvr32 /u`。ビルド dir が複数できている（ninja-debug/fix/run1/run2/r1r2）が gitignore 配下・再起動で掃除可。
- **Ollama は再インストール済み（2026-06-10・0.30.6・winget）**: 前回環境から消えていたため入れ直し、`gemma4:e4b-it-qat` を再 pull（100% GPU・変換疎通 OK）。⚠ アイドル 5 分でモデルがアンロードされ、次の変換はロード込み ~50s（WinHTTP タイムアウト 60s ギリギリ・失敗時は生ローマ字フォールバック）。常駐させるなら `OLLAMA_KEEP_ALIVE` 設定（VRAM 3.1GB 占有と引き換え）。`OllamaKanaKanjiConverter` は keep_alive 未指定（残債）。

---

## 3. 重要な技術判断・ハマりどころ
- **ビルドは pip 版 cmake/ninja を使う**（CMake 4.3.2 / Ninja 1.13.0、`%APPDATA%\Python\Python*\Scripts`）。**VS2019 同梱の CMake 3.20 はバグで使わない**（build.ps1 が自動で pip 版優先＋不在時 `requirements-build.txt` から自動導入）。
- CMake 4.x は古い `cmake_minimum_required(<3.5)` を拒否 → doctest / nlohmann json は**ヘッダオンリー取り込み**（各自の CMake プロジェクトは使わない）。
- PowerShell は裸 native 引数 `-DX=$Y` を展開しないことがある → **必ず `"-DX=$Y"` とクォート**。
- **アーキ厳守**: `yoshinani.core`（domain+application）は **Windows ヘッダ禁止**（OS非依存・テスト対象）。COM/TSF は `src/tsf` に閉じ込め、core へは値だけ渡す。
- 環境: Windows11 / **RTX 4070 Laptop 8GB** / MSVC BuildTools 2019 / Windows SDK 10.0.19041。

---

## 4. Gemma 変換 spike の実測知見（2026-06-09 / Ollama）
- Ollama 導入済み。`gemma4:e2b-it-qat`(4.3GB) と `gemma4:e4b-it-qat`(6.1GB) pull 済み。`ollama` は `%LOCALAPPDATA%\Programs\Ollama\ollama.exe`。API: `http://localhost:11434/api/generate`。
- **think=false 必須**: オンだと内部推論で 8〜20s、オフで **~1s**（GPU・~55tok/s）。
- **空白区切りのローマ字が精度の鍵**: `kyou ha openai de class wo tukutta`→「今日はOpenAIでclassを作った」◎。**空白なし連続は E4B でも英語密箇所で崩れる**（→ だから **Tab トリガーで Space を区切りに解放**した）。
- **E4B > E2B**（E2B は homograph 誤読・脱落。E4B で克服）。残課題: 英単語ホモグラフ（`node`=ので→ノード 等、ごく一部）。
- 効いたプロンプト: LazyJP式 ＋「入力全体を漏れなく・忠実に・言い換え禁止・英語/専門用語は Latin 正規化」＋少数例。詳細は [3-B](.spec/sub-specs/phase3-zenz-daemon.md) の実測表。
- **先行例 LazyJP**（`raspy135/lazyjp-vscode`）: romaji→クラウドLLM直変換・トリガー一括・**背景書き換え（async）**・継続モード（文脈チェイン）・`/rule` インラインルール。盗む価値あり。
- **追試(2026-06-09・IME 実結線後)**: 空白なし入力はプロンプトで「分かち書きを推測」と明示しても E4B では不安定（4例中2例誤り: 例 `henkan`→返金/変更）。→ 「手動スペース不要」にするには上位モデル（クラウド GPT 等）が要る、と判断。クラウド API は安価（Nano 月~$2.3）で、論点はコストよりプライバシー/遅延/「ローカル前提」を崩すか（§5 の次セッション実験へ）。

---

## 5. これから（推奨ロードマップ）
1. ~~Tab/Space 実機確認~~ **完了（2026-06-09）**: Chromium/Electron 系で Tab 確定 / Space 区切り / Esc 取消を目視 OK。標準アプリ（新メモ帳等）での TIP 非 engage は**既知課題として保留**（§2）。再ビルドは DLL ロックのため `build.ps1 -BuildDir <別dir>` で回避する。
2. **Phase 2（romaji→kana）は格下げ**: B方式では主経路に不要（zenz fallback / かな表示のときだけ）。preedit は生ローマ字表示でよい。**飛ばして Phase 3 に行ってよい**。
3. **Phase 3（本丸＝Gemma を IME に結線）**:
   - **進捗(2026-06-09)**: ✅ 3-A ポート + 3-D キュー + ユニットテスト（ctest 緑）。✅ **3-E 結線済（Ollama 代用バックエンド）= IME で romaji→Tab→Gemma→確定が Chromium 系で実動作**（commit `12f04c2`）。`src/ipc/OllamaKanaKanjiConverter`（WinHTTP→Ollama `/api/chat`）を `IKanaKanjiConverter` 実装として `TextService` に注入し、確定経路で同期変換→`CommitText` で結果確定（失敗時は生ローマ字フォールバック）。
     - 経緯: 当初ユーザーは「仕様どおり自作 llama.cpp デーモン+パイプ」を選択したが、**「今すぐ動作確認したい」を優先**し、ポートの裏に **Ollama 代用**を入れて最短で testable に到達。自作デーモン(3-B)+名前付きパイプ(3-C)は**同ポートで後から差し替え**る方針（未着手）。
     - 実機所感: 空白区切りなら実用。**空白なしは E4B では分割不安定**（プロンプト改善でも 2/4 誤り）。手動スペースの負担＝小モデルの限界とユーザーも認識。記号入力不可（R2 未実装）がストレス点。
     - 残債: `TextService` が具体実装(Ollama)を直接 new（ポート経由 DI でない）。バックエンド選択は将来 Factory/設定で。再ビルドは core のみなら `build.ps1 -Target yoshinani.core.tests`、DLL は別 dir で。
   - ~~クラウド変換 A/B~~ **完了（2026-06-10）・結線済み**。実測結果（空白なし 6 例 × 3回・seed=7）:
     | 構成 | 完全一致 | 安定 | 平均速度 |
     |--|--|--|--|
     | nano/none〜medium | 1〜2/6 | ブレ大 | 0.7〜3.5s |
     | **mini/low** | 5/6 | 5/6 | 1.5s |
     | **mini/medium（採用）** | 5/6 | **6/6** | 1.8s |
     | 5.4(フル)/none | 4/6 | 5/6 | 0.9s |
     | Gemma E4B（ローカル） | 1/6 | 安定 | 3〜4s |
     - 結論: **「手動スペース不要」は mini + low/medium で達成**（不一致分は サーバ/サーバー 等の表記揺れで意味は正解）。nano はブレ（GPT-5 系 temperature 固定）と精度で不採用。モデル ID = `gpt-5.4-nano/-mini/-5.4`、`reasoning_effort` は `none/low/medium/high/xhigh`（minimal は不可）。
     - 採用構成: **gpt-5.4-mini + medium + seed=7**（タスクトレイ UI で後々変更できる前提。settings.json で今でも変更可）。プライバシー（入力が OpenAI へ送られる）は許容と判断。
     - 実験スクリプトは `build/ab-*.ps1`（gitignore 配下）。
   - 3-A: `IKanaKanjiConverter` ポート（**モデル非依存・romaji を渡せる形**・request_id 付き非同期IF）。
   - 3-B: デーモン = llama.cpp + Gemma 4 E4B GGUF（まずは Ollama で代用可）。**think=false / 常駐(keep_alive) / num_ctx 2048**。プロンプトは §4 のもの。
   - 3-C: 名前付きパイプ IPC（TIP↔デーモン）。デーモン不在時は生ローマ字 or かなで確定するフォールバック。
   - 3-E: **Tab → preedit の空白区切りローマ字を Gemma へ → 結果を確定して手放す**。失敗時フォールバック。
   - **4-A（async/背景変換）は早めに重要**: mini/medium は ~1.8s（同期だと preedit が固まる）。LazyJP 同様「待たずに打ち続け、結果が後着」にしたい（§6.5 の継ぎ目を実働）。
   - **継続モード（文脈チェイン）をいずれ入れる（2026-06-10 ユーザー要望）**: 現状は毎回独立リクエスト（会話履歴なし）。LazyJP 式に直前の確定文を文脈として渡し、文脈依存の変換精度を上げる。`/rule` インラインルールも候補。プロンプトは `OpenAiKanaKanjiConverter.cpp` 参照。
4. 任意: zenz 速い道（全部日本語＝数十ms）を二段目に。英数密の空白なし弱点は「直すなら既存IME」哲学で許容。
5. **入力リッチ化＆モード切替（2026-06-09 ユーザー要望・subspec 反映済）**:
   - ~~R1 Shift 大文字 / R2 記号入力 / Enter 生確定~~ **完了（2026-06-10・実機確認 OK）**。
   - **残り = 1-D 入力モード切替**（変換⇄直接・[1-D](.spec/sub-specs/phase1-input-mode.md)）。TBD 確定済み: 切替キー=半角/全角・既定=変換。**open/close コンパートメント実装は §2 の標準アプリ非 engage 改善の候補**＝次の有力着手先。

---

## 6. キーファイル / コマンド早見
- **ビルド&テスト**: `pwsh -File scripts/build.ps1`（`-Clean` で再構成 / `-NoTest` でビルドのみ）。core テストのみ確認: `build/ninja-debug/tests/core/yoshinani.core.tests.exe`。
- **ランタイム確認**: `verify-ime` スキル、または `scripts/verify-ime.ps1 -Action Check|Build|Register|Unregister`。
- **Gemma 試行**: `ollama` API（localhost:11434）に `think=false` / `options.num_ctx=2048` で投げる。前セッションの PowerShell スニペット参照。
- **仕様**: `.spec/MAIN_SPEC.md`、`.spec/sub-specs/`（README 索引 + phaseN-*.md。Phase 0-4 全 spec 作成済）。
- **コード**: `src/core/domain/TriggerPolicy.*`（判定）、`src/core/application/{InputSession,Settings}.*`、`src/tsf/{TextService,Classify(内),KeyMap,Register,Globals,ClassFactory,dllmain,EditSession}.*`。
- **CLSID/Profile GUID**: `src/tsf/Globals.cpp` に固定（`CLSID_Yoshinani` = 2405C199-...、Profile = D73A3464-...）。

---

## 7. git 状態
- `main` ブランチ。2026-06-09 セッション分は**全 push 済み**（TSF カテゴリ / build.ps1 拡張 / 3-A・3-D / spec 反映 / 3-E 変換結線）。
- **2026-06-10 セッション**: ① R1/R2/Enter 実装（push 済み: 0795085, 7377634） ② クラウド A/B 実測 → `OpenAiKanaKanjiConverter` 結線＋settings.json バックエンド選択（レビュー済・ctest 緑・実機 OK）。
- 残債: ① 標準アプリ TIP 非 engage（§2） ② 1-D 入力モード切替が未実装 ③ `OllamaKanaKanjiConverter` の keep_alive 未指定 ④ `verify-ime.ps1` が `-BuildDir` 未対応（ninja-debug 固定） ⑤ 4-A async（同期 1.8s の体感改善） ⑥ 継続モード（§5）。
- `build/ninja-*`（debug/fix/run1/run2）は gitignore 配下。`%USERPROFILE%\.yoshinani\openai.key` は**リポジトリ外（コミット禁止のシークレット）**。
- コミット履歴に各フェーズの判断が残っている（`git log --oneline`）。
