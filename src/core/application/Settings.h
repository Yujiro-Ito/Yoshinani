// 設定（キーマップ等）— application 層・OS非依存。
// 将来 Google日本語入力のように設定画面からキーマップを変更できるようにするための
// 「設定データ」と「ロジック」の分離点。保存形式は JSON（[[phase1-trigger-commit]]）。
#pragma once
#include <string>
#include <vector>

namespace yoshinani::core::application {

struct Settings {
    // 確定トリガーのキー名（ポータブルな名前。各OSのキーコードへは infra が対応づける）。
    // 例: "Tab" / "Period"(。) / "Comma"(、)
    // 既定は Tab。Space（分かち書きの区切り）と Enter（生確定専用）はトリガーにできない。
    std::vector<std::string> triggerKeys{ "Tab" };
};

// JSON テキストから Settings を構築する。
// 空文字・不正JSON・キー欠落のときは既定値（triggerKeys = ["Tab"]）にフォールバックし、
// 例外は投げない（IME 内で安全に呼べるようにするため）。
Settings ParseSettings(const std::string& jsonText);

}  // namespace yoshinani::core::application
