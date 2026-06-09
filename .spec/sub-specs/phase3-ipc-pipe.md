# 3-C 名前付きパイプ IPC

## 概要

TIP（薄いクライアント）と zenz デーモン（サーバ）を**名前付きパイプ**で繋ぐ。
infra に `IKanaKanjiConverter`（3-A）のパイプ実装 `PipeKanaKanjiConverter` を置く。

## 背景・目的

- MAIN_SPEC §3/§4: プロセス分離の IPC は名前付きパイプ。TIP は結果文字列だけ受け取る。

## 設計

- infra `src/ipc/PipeKanaKanjiConverter`（`IKanaKanjiConverter` 実装）。
- プロトコル: `request_id` + `reading` を送り、`request_id` + `kanji` を受ける（1行JSON 等、軽量に）。
- デーモン側（3-B）がサーバ、TIP 側がクライアント。
- 接続・再接続・エラー処理。**デーモン未起動/失敗時はかなのまま確定にフォールバック**（3-E と協調・握りつぶさない）。

## 受け入れ条件

- [ ] TIP→デーモンへ reading 送信、kanji 受信の結合確認（手動 or 結合テスト）
- [ ] デーモン未起動時にフォールバック（かな確定）し、IME が固まらない
- [ ] 再接続が効く（デーモン再起動後に復帰）

## 影響範囲

### 新規/修正
- `src/ipc/PipeKanaKanjiConverter.{h,cpp}`（プレースホルダ Placeholder.cpp を置換）
- パイプ・プロトコル定義（client/server 共有）

## 依存関係

| SUBSPEC | 関連 |
|--|--|
| 3-A | 実装するポート |
| 3-B | サーバ側 |
| 3-D/3-E | 呼び出し元・フォールバック |

## TBD

- プロトコル形式（行区切りJSON / 長さ前置きバイナリ）と文字コード（UTF-8）
- タイムアウト値、デーモン自動起動の有無
- 結合テストをどの枠で回すか（0-C の延長）
