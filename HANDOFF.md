# HANDOFF — よしなに (Yoshinani) セッション引き継ぎ

> 最終更新: 2026-06-09。次セッションはまず本ファイル → [.spec/MAIN_SPEC.md](.spec/MAIN_SPEC.md) → [.spec/sub-specs/README.md](.spec/sub-specs/README.md) の順で読むと早い。

## 0. これは何
ローマ字を適当に打ち、**Tab で一括変換＋自動確定**する自作 Windows IME（TSF / C++）。
変換は**ローカル汎用LLM（Gemma 4）にローマ字を直渡し**して日本語にする（B方式）。
詳細仕様は MAIN_SPEC。リポジトリ: GitHub `Yujiro-Ito/Yoshinani`（`main`、ここまで全 push 済み・最新 `83ebba7`）。

---

## 1. 守るべきルール（重要）
- **コミット前コードレビュー必須**（[CLAUDE.md](CLAUDE.md)）: コード変更を含むコミットの前に `feature-dev:code-reviewer`（or `/code-review`）を実行→重大指摘を修正→ビルド&ctest緑→コミット。`.md` のみの変更はレビュー不要。
- グローバル規約（`~/.claude/CLAUDE.md`）: **常に日本語で応答** / **Bash で `cd` 禁止**（作業ディレクトリは常にプロジェクトルート）/ コミット・push は**頼まれた時だけ**。
- 大きな判断・実装完了時は「デイリーノートへ記録しますか？」と提案（ユーザーが個人作業と言ったものは提案不要）。

---

## 2. 現在地（実装済み・検証済み）
- **Phase 0（足場）完了**: ライト DDD レイヤを CMake ターゲットに分割（`yoshinani.core` / `.ipc` / `.tsf`=DLL / tests）。`scripts/build.ps1` で **vcvars 読込→cmake→ninja→ctest を1コマンド**（自己完結ループ）。
- **Phase 1（最小TIP）完了・実機確認済み**: COM登録＋TSFプロファイル(ja-JP)登録、preedit 表示、トリガー確定。以前メモ帳で `aiueo`+確定が動くのを目視済み。
- **設定分離**: トリガーキーを `settings.json`(JSON, nlohmann) に分離（`core/application/Settings` + `src/tsf/KeyMap`）。
- **トリガー = Tab、Space = 分かち書きの区切り**（最新コミット 83ebba7）。理由は §4 参照。
- **変換方針 決定済み**: B方式（romaji→汎用LLM直渡し）。モデル = **Gemma 4 E4B-it-qat**（ローカル・GGUF・Apache 2.0）、**思考オフ(think=false)必須**。

### ⚠ 未検証の保留（次セッション最優先で消化）
- **Tab/Space のランタイム挙動は未確認**。理由: ビルド時に `yoshinani.dll` が**この Claude Code プロセスにロードされてロック**され、DLL を再リンクできなかった（`LNK1168`。コードは全コンパイルOK・core ctest 緑）。
- **次セッションは別プロセス＝このロックが無い**はず。`pwsh -File scripts/build.ps1`（クリーンに通るはず）→ 後述の手順で実機確認できる。
- IME は以前 `Unregister` 済み（登録は残っていないはず。`verify-ime.ps1 -Action Check` で確認）。

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

---

## 5. これから（推奨ロードマップ）
1. **【最優先】Tab/Space を実機確認**: アプリ再起動済の今 → `scripts/build.ps1`（DLL 再リンク通るはず）→ `verify-ime.ps1 -Action Register`（UAC）→ メモ帳で「ローマ字を空白区切りで打つ → Tab で確定」「Space が区切りとして preedit に入る」を確認 → `-Action Unregister`。
2. **Phase 2（romaji→kana）は格下げ**: B方式では主経路に不要（zenz fallback / かな表示のときだけ）。preedit は生ローマ字表示でよい。**飛ばして Phase 3 に行ってよい**。
3. **Phase 3（本丸＝Gemma を IME に結線）**:
   - 3-A: `IKanaKanjiConverter` ポート（**モデル非依存・romaji を渡せる形**・request_id 付き非同期IF）。
   - 3-B: デーモン = llama.cpp + Gemma 4 E4B GGUF（まずは Ollama で代用可）。**think=false / 常駐(keep_alive) / num_ctx 2048**。プロンプトは §4 のもの。
   - 3-C: 名前付きパイプ IPC（TIP↔デーモン）。デーモン不在時は生ローマ字 or かなで確定するフォールバック。
   - 3-E: **Tab → preedit の空白区切りローマ字を Gemma へ → 結果を確定して手放す**。失敗時フォールバック。
   - **4-A（async/背景変換）は早めに重要**: 変換 ~1s なので、LazyJP 同様「待たずに打ち続け、結果が後着」にしたい（§6.5 の継ぎ目を実働）。
4. 任意: zenz 速い道（全部日本語＝数十ms）を二段目に。英数密の空白なし弱点は「直すなら既存IME」哲学で許容。

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
- `main` ブランチ、最新 `83ebba7`、全て push 済み。未コミットは（このHANDOFF.md を除き）なし。
- コミット履歴に各フェーズの判断が残っている（`git log --oneline`）。
