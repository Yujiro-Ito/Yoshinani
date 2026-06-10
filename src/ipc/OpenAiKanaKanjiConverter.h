// 3-A 実装（クラウド）: OpenAI Chat Completions を WinHTTP(HTTPS) で叩く IKanaKanjiConverter。
// A/B 実測（2026-06-10・HANDOFF §4）: gpt-5.4-mini + reasoning medium が
// 空白なしローマ字の分かち書きを実質解決（nano では不可）。
// API キーは %USERPROFILE%\.yoshinani\openai.key（1行）から読む。
// キーと Authorization ヘッダは絶対にログ・例外・出力へ出さないこと。
#pragma once
#include <string>

#include "domain/ports/IKanaKanjiConverter.h"

namespace yoshinani::ipc {

class OpenAiKanaKanjiConverter final : public yoshinani::core::domain::IKanaKanjiConverter {
public:
    OpenAiKanaKanjiConverter(std::string model = "gpt-5.4-mini",
                             std::string reasoningEffort = "medium");

    // 同期実装（v1）。HTTPS 往復してから onDone を呼ぶ（§6.5 ①「形だけ非同期」）。
    // キー不在・通信失敗は ok=false（呼び出し側が生入力フォールバック／3-E）。
    void Convert(yoshinani::core::domain::RequestId id,
                 const yoshinani::core::domain::ConversionInput& input,
                 std::function<void(yoshinani::core::domain::RequestId,
                                    yoshinani::core::domain::ConversionResult)> onDone) override;

private:
    std::string  model_;
    std::string  reasoningEffort_;
    std::wstring apiKey_;  // 起動時に1回読込（空 = キー不在 → 常に ok=false）
};

}  // namespace yoshinani::ipc
