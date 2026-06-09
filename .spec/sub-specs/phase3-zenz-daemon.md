# 3-B zenz デーモン（llama.cpp + GGUF）

## 概要

ひらがな→漢字を行う **zenz (GGUF) を llama.cpp で動かす別プロセス常駐デーモン** `yoshinanid`。
TIP（全アプリにロードされる薄いDLL）にモデルを載せない（MAIN_SPEC §3）ための分離。

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
