# 4-E Android アダプタ

## 概要

`yoshinani.core`（romaji管理 / トリガー判定 / 変換ポート / キュー / 設定）を**再利用**し、
Android の **IMS（InputMethodService, Kotlin）＋キーボードUI** をアダプタとして実装する。
MAIN_SPEC §8「コアとアダプタの分離」の回収。

## スコープ

- core を Android（NDK）でビルド／FFI 経由で利用。
- Android アダプタ：キーボードUI、IMS ライフサイクル、キー→core への橋渡し（VK 相当の対応は Android 側）。
- zenz+llama.cpp は Android 動作実績あり（Sumire 等・MAIN_SPEC §8）。

## 前提

- 0-A の「core に Windows を混ぜない」が守られていること（守られていれば移植リスク低）。
- Phase 1〜3 完了（core が安定）。

## 依存関係

| SUBSPEC | 関連 |
|--|--|
| 0-A | コア/アダプタ分離の前提 |
| 2-A/3-* | 再利用する core ロジック |
| 1-C 設定分離 | キーマップはキー名で持つため Android 側の対応づけがしやすい |

## TBD

- core の Android ビルド（CMake/NDK）と FFI 方式（JNI 直 / ブリッジ）
- キーボードUI の設計
- zenz デーモン相当を Android でどう常駐させるか（プロセス分離 or in-process）
