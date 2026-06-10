// 設定（キーマップ等）— application 層・OS非依存。
// 将来 Google日本語入力のように設定画面からキーマップを変更できるようにするための
// 「設定データ」と「ロジック」の分離点。保存形式は JSON（[[phase1-trigger-commit]]）。
#pragma once
#include <string>
#include <vector>

namespace yoshinani::core::application {

// 変換バックエンドの設定。タスクトレイ UI（yoshinani-tray.exe）はこの JSON を書き換えるだけで反映できる。
// A/B 実測（2026-06-10）: gpt-5.4-mini + low/medium が空白なしローマ字の分かち書きを実質解決
// （nano では不可）。既定は low（medium と精度僅差で、思考トークン課金が数分の1・HANDOFF §4）。
//
// 空文字の扱い: 各フィールドの空文字は「指定なし＝既定を保つ」のシノニム。
//   ParseSettings は空文字をスキップし、SerializeSettings は空文字をそのまま書き出す。
//   ラウンドトリップ（write → read）では既定値に戻るのが意図的挙動（トレイ UI の
//   「(バックエンド既定)」メニュー項目はこの規約に乗っている）。
struct ConverterSettings {
    std::string backend{ "openai" };       // "openai" | "ollama"
    std::string model{};                   // 空 = バックエンド既定（openai: gpt-5.4-mini / ollama: gemma4:e4b-it-qat）
    std::string reasoningEffort{ "low" };  // openai のみ: none/low/medium/high/xhigh
};

struct Settings {
    // 確定トリガーのキー名（ポータブルな名前。各OSのキーコードへは infra が対応づける）。
    // 例: "Tab" / "Period"(。) / "Comma"(、)
    // 既定は Tab。Space（分かち書きの区切り）と Enter（生確定専用）はトリガーにできない。
    std::vector<std::string> triggerKeys{ "Tab" };
    // 入力モード切替（変換⇄直接・1-D）のキー名。既定は半角/全角キー。
    std::vector<std::string> modeToggleKeys{ "Kanji" };
    ConverterSettings converter{};
};

// JSON テキストから Settings を構築する。
// 空文字・不正JSON・キー欠落のときは既定値（triggerKeys = ["Tab"]）にフォールバックし、
// 例外は投げない（IME 内で安全に呼べるようにするため）。
Settings ParseSettings(const std::string& jsonText);

// Settings を JSON テキストへ書き出す（タスクトレイ UI が settings.json を更新するため）。
// インデント2の人間可読フォーマット。ParseSettings とラウンドトリップする。
std::string SerializeSettings(const Settings& s);

}  // namespace yoshinani::core::application
