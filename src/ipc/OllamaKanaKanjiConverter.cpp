// 3-A 実装（v1 stand-in）: Ollama(Gemma) を WinHTTP で叩いて romaji→日本語に変換する。
// 失敗・デーモン不在時は ok=false を返し、呼び出し側(3-E)が生入力のまま確定する。
#include "OllamaKanaKanjiConverter.h"

#include <nlohmann/json.hpp>

#include <utility>

#include "HttpJson.h"
#include "Utf.h"

namespace yoshinani::ipc {

using yoshinani::core::domain::ConversionResult;
using yoshinani::core::domain::RequestId;

OllamaKanaKanjiConverter::OllamaKanaKanjiConverter(std::wstring host, int port, std::string model)
    : host_(std::move(host)), port_(port), model_(std::move(model)) {}

void OllamaKanaKanjiConverter::Convert(
        RequestId id, std::u16string_view input,
        std::function<void(RequestId, ConversionResult)> onDone) {
    const std::string in = U16ToU8(input);

    // §4 実測で効いたプロンプト（LazyJP 式＋忠実変換＋Latin 正規化＋少数例）。
    // ※ /utf-8 でコンパイルするため日本語リテラルは UTF-8 で格納される。
    const std::string prompt =
        "次の「空白区切りローマ字」を自然な日本語（漢字かな交じり）に変換してください。\n"
        "ルール: 入力を漏れなく忠実に変換し、言い換え・要約・補足説明をしない。"
        "英語/専門用語/固有名詞は Latin 文字のまま残す。出力は変換後の日本語のみ。引用符や説明を付けない。\n\n"
        "例:\n入力: kyou ha openai de class wo tukutta\n出力: 今日はOpenAIでclassを作った\n\n"
        "入力: " + in + "\n出力:";

    nlohmann::json req;
    req["model"]                 = model_;
    req["stream"]                = false;
    req["think"]                 = false;
    req["options"]["num_ctx"]    = 2048;
    req["options"]["temperature"] = 0;
    req["messages"]              = nlohmann::json::array();
    req["messages"].push_back({{"role", "user"}, {"content", prompt}});

    std::string resp;
    const bool ok = PostJson(host_, port_, /*secure=*/false, L"/api/chat", L"", req.dump(), resp);

    ConversionResult result;
    if (ok) {
        try {
            auto j = nlohmann::json::parse(resp);
            if (j.contains("message") && j["message"].contains("content")) {
                result.text = U8ToU16(Trim(j["message"]["content"].get<std::string>()));
                result.ok   = !result.text.empty();
            }
        } catch (...) {
            // 不正レスポンスは ok=false のまま（フォールバック）。
        }
    }
    onDone(id, std::move(result));
}

}  // namespace yoshinani::ipc
