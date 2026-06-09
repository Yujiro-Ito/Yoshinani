# 3-B 変換デーモン（llama.cpp + GGUF）

## 概要

ローマ字→日本語を行う **変換LLM（GGUF）を llama.cpp で動かす別プロセス常駐デーモン** `yoshinanid`。
TIP（全アプリにロードされる薄いDLL）にモデルを載せない（MAIN_SPEC §3）ための分離。

> 更新(2026-06-09): 当初は zenz(かな専用) 想定だったが、**B方式（ローマ字直渡し）に伴い主モデルを汎用LLM Gemma 4 に変更**。zenz は「全部日本語の速い道/fallback」として残す。

## 実測知見（2026-06-09・Ollama で spike）

`Gemma 4 E2B-it-qat`（4.3GB）/ `E4B-it-qat`（6.1GB）を RTX4070(8GB) で計測した結果:

| 項目 | 結論 |
|--|--|
| **採用モデル** | **Gemma 4 E4B-it-qat（GGUF・Apache 2.0）**。E2B は英語混在・空白なしで崩れた（例: tukutta→読書した）。E4B は homograph/脱落を克服 |
| **思考(reasoning)** | **必ずオフ（think=false）**。オンだと内部推論で 8〜20s。オフで **~1s** に激減（GPU・~55tok/s、遅さの正体は推論トークン） |
| **常駐** | モデルは VRAM 常駐（keep_alive）。初回コールドロードは十数秒なので落とさない |
| **入力形式** | **空白区切りのローマ字が必須級**。`kyou ha openai de class wo tukutta`→「今日はOpenAIでclassを作った」◎。空白なし密英語は E4B でも崩れる → §5 で **Tab トリガー＋Space 区切り** に決定 |
| **プロンプト** | LazyJP 式＋「入力全体を漏れなく・忠実に・言い換え禁止・英語/専門用語は Latin 正規化」＋少数例。脱落対策に有効 |
| **残課題** | 英単語ホモグラフ（`node`=ので を ノード 等）。ごく一部。"直すなら既存IME" 哲学で許容 |
| **context(num_ctx)** | 2048 で十分・KV 小さく高速 |

> 速度比較: zenz(90M, かな専用) は数十ms 級で圧倒的に速い。全部日本語のときは zenz 速い道、英語混じり/文脈は Gemma、の二段が理想。

## 背景・目的

- MAIN_SPEC §4: kana→kanji は **zenz (Miwa-Keita/zenz, GGUF, 変換専用)**、実行基盤は **llama.cpp**。
- TIP に同居させると全アプリが重くなる → 別プロセス常駐＋IPC（3-C）。

## 設計

- ターゲット `yoshinanid`（EXE、0-B の `YOSHINANI_BUILD_DAEMON`）。
- llama.cpp 取り込み（FetchContent / submodule。daemon だけが依存）。
- 起動時に GGUF をロード（パスは引数 or 設定）。常駐し、3-C のパイプサーバとしてリクエスト受信。
- zenz は変換専用モデル → reading(かな) を入力に kanji 列を生成（プロンプト/トークナイズは要調整）。
- GGUF はリポジトリに含めない（`.gitignore` で `*.gguf` 除外済み）。

## 受け入れ条件

- [ ] デーモン単体で、かな入力に対し妥当な漢字交じり文を返す（手動・例: 「きょうはいいてんき」→「今日はいい天気」相当）
- [ ] 常駐し、複数リクエストを順に処理できる
- [ ] モデル未配置時に分かるエラーを返す（握りつぶさない）

## 影響範囲

### 新規
- `src/daemon/`（main、llama ラッパ、パイプサーバ）
- llama.cpp 取り込み設定（0-B）

## 依存関係

| SUBSPEC | 関連 |
|--|--|
| 3-C | IPC プロトコル（サーバ側） |
| 0-B | llama.cpp ビルド統合 |
| 4-D | 速度最適化（投機的デコード等） |

## TBD

- GGUF の配布・配置（DL手順／既定パス）
- zenz のプロンプト設計・トークナイズ・出力整形
- 起動/常駐管理（IME 起動時に自動起動するか、別途常駐か）
- 冷起動レイテンシ、メモリ使用量
