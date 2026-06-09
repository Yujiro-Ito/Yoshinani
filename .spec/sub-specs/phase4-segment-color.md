# 4-B 変換中セグメントの色分け表示

## 概要

`ITfDisplayAttributeProvider` を実装し、preedit の状態（入力中 / 変換待ち / 変換済み）を
**下線・色で区別**して表示する。1-B では既定描画に任せていた部分の本実装。

## スコープ

- `ITfDisplayAttributeProvider` / 表示属性の登録（`GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER`）。
- セグメントごとの表示属性適用（async/B方式の複数セグメントと連動）。

## 前提

- 1-B（composition）動作済み。4-A（複数セグメント）と相性が良い。

## 依存関係

| SUBSPEC | 関連 |
|--|--|
| 1-B | composition 基盤 |
| 4-A | 変換中セグメントの可視化 |

## TBD

- 表示属性 GUID の登録（DllRegisterServer への追加）
- 色・下線スタイルの具体
