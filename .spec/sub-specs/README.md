# よしなに (Yoshinani) — サブスペック索引

`MAIN_SPEC.md`（`.spec/MAIN_SPEC.md`）のロードマップを SDD（仕様駆動開発）で実装するための
サブスペック群。各フェーズは「最小の縦スライス」で完結させ、Phase 1〜3 でプロダクトが成立する。

- 元仕様: [MAIN_SPEC.md](../MAIN_SPEC.md)
- 開発手法: SDD + ライト DDD（domain / application / infrastructure 分離）
- テスト方針: 厳密な TDD ではなく「**最後にテストが揃っていればよい**」。
  そのため**テストしたい純粋ロジックを `yoshinani.core` に隔離**しておく（→ [0-A](phase0-architecture.md)）。

---

## ビルド & 検証（自己完結ループ・CLI 完結）

VS IDE 不要。**現行版 CMake + Ninja** を使う（`build.ps1` が不在時に
[`scripts/requirements-build.txt`](../../scripts/requirements-build.txt) から pip で自動導入）。

```powershell
pwsh -File scripts/build.ps1            # vcvars 読込 → (必要なら cmake/ninja 自動導入) → configure → build → ctest
pwsh -File scripts/build.ps1 -Clean     # クリーン再構成
pwsh -File scripts/verify-ime.ps1 -Action Check   # TSF ランタイム確認の前提点検
```

> 前提（別PCの初回のみ）: **VS Build Tools +「C++によるデスクトップ開発」**（`cl.exe`/Windows SDK）。
> これだけは pip では入らないので手動導入が必要。cmake/ninja は `build.ps1` が自動で入れる。

- 自動: `yoshinani.core` のユニットテスト（`ctest`）。これは私（エージェント）が人手なしで回せる。
- 手動/MCP: 実アプリでの preedit 目視は [verify-ime スキル](../../.claude/skills/verify-ime/SKILL.md)
  （Windows-MCP / computer-use。未整備なら実行エラーで通知）。
- 詳細・実測ツールチェーン・踏んだ落とし穴は [0-B](phase0-build-system.md) を参照。

> 進捗: **Phase 0・Phase 1 実装済み**（最小 TIP が登録され、メモ帳で preedit→Space 確定を実機確認）。
> トリガーは `Space`（設定 JSON で変更可）。Phase 2 以降は spec 作成済・実装はこれから。

---

## フェーズ一覧

| ID | サブスペック | SPEC対応 | 状態 |
|--|--|--|--|
| **0-A** | [アーキテクチャ方針](phase0-architecture.md) | §3,§8 | ✅ 作成済 |
| **0-B** | [ビルド/プロジェクト構成](phase0-build-system.md) | §4 | ✅ 作成済 |
| **0-C** | [テスト土台](phase0-test-harness.md) | — | ✅ 作成済 |
| **1-A** | [TSF スケルトン & COM 登録](phase1-tsf-skeleton.md) | Step1 §7 | ✅ 作成済 |
| **1-B** | [preedit ライフサイクル](phase1-preedit-lifecycle.md) | Step1 §7 | ✅ 作成済 |
| **1-C** | [トリガー確定](phase1-trigger-commit.md) | Step1 §5,§7 | ✅ 作成済 |
| **1-D** | [入力モード切替（変換/直接・Google IME 準拠）](phase1-input-mode.md) | Step1+ | 🆕 要件記録(2026-06-09) |
| 2-A | [romaji→kana 決定的変換](phase2-romaji-kana.md) | Step2 | ✅ 作成済 |
| 2-B | [preedit への結線](phase2-preedit-integration.md) | Step2 | ✅ 作成済 |
| 3-A | [変換ポート抽象（request_id 付き非同期IF / §6.5①）](phase3-converter-port.md) | Step3 | ✅ 作成済 |
| 3-B | [zenz デーモン（llama.cpp + GGUF）](phase3-zenz-daemon.md) | Step3 | ✅ 作成済 |
| 3-C | [名前付きパイプ IPC](phase3-ipc-pipe.md) | Step3 | ✅ 作成済 |
| 3-D | [変換キュー & 投入順確定（§6.5②③）](phase3-conversion-queue.md) | Step3 | ✅ 作成済 |
| 3-E | [A 自動確定の結線](phase3-auto-commit.md) | Step3 | ✅ 作成済 |
| 4-A | [async 化（B方式）](phase4-async.md) | Step4 | ✅ 作成済 |
| 4-B | [変換中セグメント色分け](phase4-segment-color.md) | Step4 | ✅ 作成済 |
| 4-C | [固有名詞辞書 / 文体プロンプト](phase4-dictionary-style.md) | Step4 | ✅ 作成済 |
| 4-D | [速度最適化](phase4-speed-opt.md) | Step4 | ✅ 作成済 |
| 4-E | [Android アダプタ](phase4-android-adapter.md) | Step4,§8 | ✅ 作成済 |

★ **Phase 1〜3 でプロダクト完結**。Phase 4 は全て「あったら嬉しい」枠。

> 📌 入力リッチ化の追加要件(2026-06-09): **R1 Shift で大文字 / R2 記号も preedit に入力**は
> [1-B preedit ライフサイクル](phase1-preedit-lifecycle.md#拡張要件2026-06-09-追加--b方式の入力リッチ化)、
> **Enter の Google IME 準拠挙動**は [1-C トリガー確定](phase1-trigger-commit.md) に追記済み。
> **入力モード切替**は新規 [1-D](phase1-input-mode.md)。いずれも未実装。

## 実装順序（推奨）

```
0-A 憲法 → 0-B ビルド → 0-C テスト枠
   → 1-A TSF登録 → 1-B preedit → 1-C トリガー確定   ← Step1: 最大の壁を突破
   → 2-A romaji→kana → 2-B 結線                      ← Step2: ローマ字かな入力成立
   → 3-A ポート → 3-B デーモン → 3-C IPC → 3-D キュー → 3-E 確定  ← Step3: プロダクト完成
```
