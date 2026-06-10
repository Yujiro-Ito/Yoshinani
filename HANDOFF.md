# HANDOFF — よしなに (Yoshinani) セッション引き継ぎ

> 最終更新: 2026-06-10 後半（タスクトレイ EXE / IME プロファイルアイコン / 片方向遷移キー / Keymap キャプチャ UI）。次セッションはまず本ファイル → [.spec/MAIN_SPEC.md](.spec/MAIN_SPEC.md) → [.spec/sub-specs/README.md](.spec/sub-specs/README.md) の順で読むと早い。

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
- **1-D 入力モード切替 完了（2026-06-10・コミット `c308a8d`）**: 半角/全角キーで 変換⇄直接 を切替（settings.json `modeToggleKeys`・既定 "Kanji"＝VK_KANJI/OEM_AUTO/OEM_ENLW をエイリアス）。モード状態は core `InputModeState`、TSF は `GUID_COMPARTMENT_KEYBOARD_OPENCLOSE` と双方向同期（`ITfCompartmentEventSink` で言語バー/アプリ側切替にも追従）。直接入力モードは切替キー以外すべて素通し＝コーディング可。切替/外部 close 時は未確定を生確定して保護（`FlushAsRaw`・sink からは async edit session）。**activate 時は常に変換モードで開始**（open/close はスレッド共有のため直前の別 IME の close 値を引き継ぐと「選んだのに動かない」になるのを防ぐ）。⚠ ランタイム実機確認は本コミット版では未実施（前回の動作確認はビルド乱れ区間と重なり信頼できない。次回まず実機確認すること）。
- **タスクトレイ EXE + 設定ウィンドウ 完了（2026-06-10 後半・最終 `692e646`）**: `src/tray/` に `yoshinani-tray.exe` 追加（Win32 GUI・Mutex 二重起動防止・HWND_MESSAGE 常駐）。通知領域メニューは「設定を開く / 終了」のみ、左クリック1発で設定 Window を開く。設定 Window はタブ付き（General / Keymap / Model）。General＝settings.json パス表示 + JSON/フォルダを開く、Model＝バックエンド連動（OpenAI/Ollama）でモデル ComboBox を入替・reasoningEffort は OpenAI のときだけ Enable、Keymap＝**キーキャプチャ UI**（「記録」→押したキーを VkToTrayName で名前化して EDIT に。Esc キャンセル・クリア有り）。Visual Styles マニフェスト同梱（CMake は `/MANIFEST:NO` で自動マニフェストを抑制し .rc 側の `1 RT_MANIFEST` を使う）。書き込みは tmp + MoveFileEx の原子置換。
- **設定ファイルパス統一**: `%APPDATA%\yoshinani\settings.json` を正規、TIP `LoadSettings` は「APPDATA 優先 → DLL 同居フォールバック」。トレイ EXE の書込先と TIP の読込先が同じパスに合流。SHGetKnownFolderPath(FOLDERID_RoamingAppData) を `src/tsf/CMakeLists.txt` に shell32 リンクして使用。
- **TIP プロファイルアイコン 完了（コミット `3bdf3ce`）**: `assets/yoshinani.ico`（暖簾「よ」・256/64/48/40/32/24/20/16 マルチサイズ・MakeTransparent で背景透過）を `src/tsf/yoshinani-tsf.rc` 経由で DLL に埋め込み、Register.cpp の AddLanguageProfile に `GetModuleFileNameW` で取った DLL 絶対パス + `uIconIndex=0`（MSDN 仕様: 0-based index）を渡す。設定 > 言語 / Win+Space / 言語バー に暖簾アイコンが表示される。**現在 register されている DLL**: `build/ninja-imeicon/src/tsf/yoshinani.dll`（コミット `3bdf3ce` 時点で `regsvr32 /u` 旧 → `regsvr32` 新の差替済み・実機反映確認はユーザー目視）。
- **片方向モード遷移キー 完了（コミット `afc5b5f`）**: Google 日本語入力流に `Settings.conversionOnKeys`（直接→変換）/ `Settings.directOnKeys`（変換→直接）を追加（既存 `modeToggleKeys` は両方向トグルとして残置）。TIP の OnTestKeyDown/OnKeyDown で「トグル / 片方向 ConvOn / 片方向 DirectOn」の 3 系統を統一判定。順序固定: `m_mode.Set/Toggle` → `SetOpenCloseCompartment`（OnChange の同期発火による二重反転を防ぐ）。KeyMap に `NonConvert/Convert/Capital` を追加（`Hiragana` は SDK 未定義のため NOTE で保留）。⚠ ランタイム実機確認は未実施（次回タスク #16 の検証時と併せて）。
- **変換方針 決定済み**: B方式（romaji→汎用LLM直渡し）。モデル = **Gemma 4 E4B-it-qat**（ローカル・GGUF・Apache 2.0）、**思考オフ(think=false)必須**。

### 検証結果（2026-06-09）と既知課題
- **Tab/Space ランタイム確認を実施**。結果は**アプリ依存**:
  - **Chromium/Electron 系（Claude アプリ・VSCode・Chrome 等）= 完全動作**: 下線付き preedit / Space が区切りとして preedit に入る / Tab 確定 / Esc 取消、すべて目視 OK。**Phase 3 はこの環境で開発・検証できる**。
  - **Win11 新メモ帳など標準アプリ = TIP が入力に engage しない**: キーが全く eaten されず素通し（直接入力・下線なし・Tab/Esc 無反応）。DLL 自体はメモ帳にもロードされる（列挙目的）が `OnKeyDown` が発火していない。
- **🔴 既知課題: 標準 Win32/パッケージアプリでの TIP 非 engage**。`GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT` + `SYSTRAYSUPPORT` をカテゴリ登録に追加したが**それだけでは未解決**（必要だが不十分）。残る有力候補: キーボード open/close コンパートメント未設定 / フォーカス・表示属性まわりの実装不足。次に着手するなら **TIP に軽量ログ（Activate/OnKeyDown の発火をアプリ別に記録）で局所化**が確実。**ユーザー判断で当面保留 → Phase 3（B 方式の本丸）を優先**。
- **DLL ロックは広範化**: register により `yoshinani.dll` が explorer.exe / Chrome / Unity / Claude などに常駐ロード。元パス `build/ninja-debug/.../yoshinani.dll` は reboot 級でないと再リンク不可。→ 当面は **`build.ps1 -BuildDir build/ninja-fix` 等の別ディレクトリ出力で回避**（実施済み）。恒久対策案（ビルド出力とは別ファイルを登録して「ビルド出力＝誰もロードしない」分離）は保留中。「毎回ユニーク名コピー」案は増え続けるため不採用で確定。
- **クラウド変換 結線済み（2026-06-10・実機 OK）**: `OpenAiKanaKanjiConverter`（WinHTTP HTTPS→`/v1/chat/completions`）を追加し、settings.json の `converter`（backend/model/reasoningEffort）で選択可能に（`TextService` の直接 new 残債①解消）。**既定 = openai / gpt-5.4-mini / medium**。キーは `%USERPROFILE%\.yoshinani\openai.key`（配置済み・コミット禁止）。キー不在/失敗は生ローマ字フォールバック。**空白なしローマ字が実機で変換できることを確認**（長文・難文も OK。誤字は「忠実変換」ルールにより音のまま残る＝仕様）。コピペ文字列は IME を経由しないので変換不可（TSF の仕様）。
- **4-A async + 4-B 下線区別 完了・実機確認 OK（2026-06-10・ユーザー高評価）**: Tab で打鍵中セグメントを `ConversionQueue`(容量8) に enqueue → ワーカースレッド変換（`ConvertMarshaller` がメッセージ専用ウィンドウで TIP スレッドへ結果を返送）→ **投入順に先頭だけ確定**（`ITfComposition::ShiftStart` で composition を縮小）。変換中=点線下線 / 入力中=実線下線（`ITfDisplayAttributeProvider`）。Esc=全取消（遅延結果は自然無視）/ Enter=全生確定 / Backspace=打鍵中のみ / 満杯時 Tab 無視。
- **コスト知見＆既定変更**: reasoning medium は思考トークンが output 課金され検証 ~150 コールで $0.10（月3万変換なら ~$20 規模）→ **既定 reasoningEffort を low に変更**（A/B で medium と精度僅差・数分の1のコスト）。
- **Claude CLI バックエンド案は保留**: `claude -p --model haiku` は品質良好だが単発起動が遅い。`--bare` は高速化するが**サブスク認証(OAuth)非対応**で 401。常駐（Agent SDK + stream-json、3-B/3-C のデーモン+パイプ設計に載る・継続モードの土台にもなる）が本命だが、**2026/6/15 以降 headless はサブスクと別クレジット枠**の情報があり要確認 → 着手前に料金体系を確認すること。
- **継続モード（文脈チェイン）完了・実機 OK（2026-06-10）**: 確定文を `ContextHistory`（直近3件・240字・core/テスト済）に積み、変換時にプロンプトの文脈節として渡す（LazyJP 相当）。「だけん→打鍵」等の同音異義・専門語が前文の話題で当たりやすくなる。ポートは `ConversionInput{source, context}` に拡張（破壊的変更・全実装更新済み）。ユーザー辞書（4-C）は「登録の手間・PC間共有が面倒」で不採用、継続モードを優先（ユーザー判断）。
- **現在 IME は登録されたまま**（継続モード版 `build/ninja-ctx1/src/tsf/yoshinani.dll`・2026-06-10 切替済み。隣に `settings.json`（reasoningEffort=low）配置済み）。Chromium 系で **romaji→Tab→Gemma→日本語確定が実動作**。再ビルドは別 dir 必須（ロック）。不要になったら `regsvr32 /u`。ビルド dir が複数できている（ninja-debug/fix/run1/run2/r1r2）が gitignore 配下・再起動で掃除可。
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
   - ~~4-A（async/背景変換）~~ **完了（2026-06-10・4-B 下線区別と同時・実機 OK）**。
   - ~~継続モード（文脈チェイン）~~ **完了（2026-06-10・実機 OK）**。`/rule` インラインルールは未実装の将来候補。
4. 任意: zenz 速い道（全部日本語＝数十ms）を二段目に。英数密の空白なし弱点は「直すなら既存IME」哲学で許容。
5. **入力リッチ化＆モード切替（2026-06-09 ユーザー要望・subspec 反映済）**:
   - ~~R1 Shift 大文字 / R2 記号入力 / Enter 生確定~~ **完了（2026-06-10・実機確認 OK）**。
   - ~~1-D 入力モード切替~~ **実装完了（2026-06-10・`c308a8d`・実機未確認）**。open/close コンパートメント実装が §2 の標準アプリ非 engage を改善するかは次回実機で要検証。
6. **タスクトレイ・設定 UI・モード切替の発展（2026-06-10 後半）**:
   - ~~タスクトレイ EXE + 設定ウィンドウ~~ **完了（`692e646`）**。Keymap タブはキーキャプチャ UI（記録ボタン → キー押下で割当）。
   - ~~settings.json を %APPDATA%\yoshinani\ に統一~~ **完了**。
   - ~~TIP プロファイルアイコン（暖簾）~~ **完了（`3bdf3ce`）**。
   - ~~片方向モード遷移キー（conversionOnKeys / directOnKeys）~~ **完了（`afc5b5f`）**。
7. **次の磨き候補**:
   - **#22 ITfLangBarItemButton 動的アイコン**（直接=「A」/ 変換=暖簾）。Mozc 級の言語バーアイテム実装が要る・規模大。Win10/11 の Modern Input Indicator が表示権限を握る場合があり、効きを実機 PoC で確認する必要あり。
   - Keymap タブの複数キー対応（現状は各項目 1 キー）。「+ 追加」「× 削除」で vector に複数要素を入れる UI へ。
   - トレイ EXE の自動起動（スタートアップ登録 or タスクスケジューラ）。

---

## 6. キーファイル / コマンド早見
- **ビルド&テスト**: `pwsh -File scripts/build.ps1`（`-Clean` で再構成 / `-NoTest` でビルドのみ）。core テストのみ確認: `build/ninja-debug/tests/core/yoshinani.core.tests.exe`。
- **ランタイム確認**: `verify-ime` スキル、または `scripts/verify-ime.ps1 -Action Check|Build|Register|Unregister`。
- **Gemma 試行**: `ollama` API（localhost:11434）に `think=false` / `options.num_ctx=2048` で投げる。前セッションの PowerShell スニペット参照。
- **仕様**: `.spec/MAIN_SPEC.md`、`.spec/sub-specs/`（README 索引 + phaseN-*.md。Phase 0-4 全 spec 作成済）。
- **コード**: `src/core/domain/TriggerPolicy.*`（判定）、`src/core/application/{InputSession,Settings}.*`、`src/tsf/{TextService,Classify(内),KeyMap,Register,Globals,ClassFactory,dllmain,EditSession}.*`、`src/tray/{main,SettingsWindow}.cpp`（タスクトレイ + 設定 GUI）、`assets/yoshinani.ico`（暖簾アイコン）。
- **CLSID/Profile GUID**: `src/tsf/Globals.cpp` に固定（`CLSID_Yoshinani` = 2405C199-...、Profile = D73A3464-...）。
- **設定 JSON 場所**: `%APPDATA%\yoshinani\settings.json`（正規）・DLL 同居 settings.json はフォールバック。
- **タスクトレイ起動**: `build/ninja-modekey/src/tray/yoshinani-tray.exe`（最新ビルド・スタートアップ登録は未実装）。
- **DLL 登録切替**: `regsvr32 /u /s "<old.dll>"; regsvr32 /s "<new.dll>"`（管理者権限）。CLSID `{2405C199-9A3D-47FC-A662-184378973C12}` の `HKLM\SOFTWARE\Classes\CLSID\<...>\InProcServer32` の `(default)` が現在の登録 DLL。

---

## 7. git 状態
- `main` ブランチ。2026-06-09 セッション分は**全 push 済み**（TSF カテゴリ / build.ps1 拡張 / 3-A・3-D / spec 反映 / 3-E 変換結線）。
- **2026-06-10 前半**: ① R1/R2/Enter 実装 ② クラウド A/B → `OpenAiKanaKanjiConverter` 結線 ③ 4-A async + 4-B 下線区別＋既定 effort=low ④ 継続モード（文脈チェイン） ⑤ **1-D 入力モード切替**（`c308a8d`。①〜④はレビュー済・実機 OK。⑤はレビュー済・ctest 緑だが**実機未確認**）。
- **2026-06-10 後半**（push 状況は `git status -sb` で要確認）:
  - `54ed438` Phase1+ タスクトレイ UI 初版（backend/model/effort 通知領域メニュー）
  - `5f4e891` タスクトレイを設定ウィンドウ＋暖簾アイコンに刷新（タブ付き GUI）
  - `3bdf3ce` TIP DLL に IME プロファイルアイコン埋め込み（AddLanguageProfile + DLL パス + uIconIndex=0）
  - `afc5b5f` モード切替を片方向2系統に拡張（conversionOnKeys/directOnKeys） + 設定 UI 修正（General 最左・ラベル見切れ解消）
  - `692e646` Keymap タブをキーキャプチャ UI に（記録ボタン → 押下キーを名前で割当）
- 残債:
  - **1-D + 片方向モード遷移キーの実機確認**（メモ帳含む。標準アプリ engage が 4-B/1-D で改善したかも併せて検証）
  - **#22 ITfLangBarItemButton 動的アイコン**（直接=「A」/ 変換=暖簾。Mozc 級実装・規模大）
  - Keymap タブの複数キー対応（現状 1 項目 1 キーのみ）
  - トレイ EXE の自動起動（スタートアップ登録）
  - `OllamaKanaKanjiConverter` の keep_alive 未指定 / `verify-ime.ps1` が `-BuildDir` 未対応 / Claude CLI バックエンド料金確認 / `/rule` インラインルール（将来候補）
  - `.agents/` / `AGENTS.md` 未コミット（Codex 用設定の残り・今セッションでは触らず温存）
- `build/ninja-*`（debug/fix/run1/run2/tray/imeicon/modekey 等）は gitignore 配下。`%USERPROFILE%\.yoshinani\openai.key` は**リポジトリ外（コミット禁止のシークレット）**。
- コミット履歴に各フェーズの判断が残っている（`git log --oneline`）。
