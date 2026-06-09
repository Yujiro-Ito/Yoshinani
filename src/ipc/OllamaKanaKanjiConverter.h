// 3-A 実装（v1 stand-in）: Ollama(Gemma) を WinHTTP で叩いて変換する IKanaKanjiConverter。
// 将来は自作 llama.cpp デーモン + 名前付きパイプ(3-B/3-C)へ「同じポート」で差し替え可能。
#pragma once
#include <string>

#include "domain/ports/IKanaKanjiConverter.h"

namespace yoshinani::ipc {

class OllamaKanaKanjiConverter final : public yoshinani::core::domain::IKanaKanjiConverter {
public:
    OllamaKanaKanjiConverter(std::wstring host = L"localhost",
                             int port = 11434,
                             std::string model = "gemma4:e4b-it-qat");

    // 同期実装（v1）。HTTP 往復してから onDone を呼ぶ（§6.5 ①「形だけ非同期」）。
    void Convert(yoshinani::core::domain::RequestId id,
                 std::u16string_view input,
                 std::function<void(yoshinani::core::domain::RequestId,
                                    yoshinani::core::domain::ConversionResult)> onDone) override;

private:
    std::wstring host_;
    int          port_;
    std::string  model_;
};

}  // namespace yoshinani::ipc
