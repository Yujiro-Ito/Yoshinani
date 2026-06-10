// 3-A 実装（クラウド）: OpenAI Chat Completions で romaji→日本語に変換する。
#include "OpenAiKanaKanjiConverter.h"

#include <windows.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <utility>

#include "HttpJson.h"
#include "Utf.h"

namespace yoshinani::ipc {

using yoshinani::core::domain::ConversionResult;
using yoshinani::core::domain::RequestId;

namespace {

// %USERPROFILE%\.yoshinani\openai.key の先頭行を返す（無ければ空）。
// 値はシークレット。呼び出し側含めログ・出力に載せない。
std::string ReadApiKey() {
    wchar_t profile[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"USERPROFILE", profile, ARRAYSIZE(profile));
    if (n == 0 || n >= ARRAYSIZE(profile)) return {};
    std::wstring path(profile);
    path += L"\\.yoshinani\\openai.key";
    std::ifstream f(path.c_str());
    if (!f) return {};
    std::string line;
    std::getline(f, line);
    // 前後空白・BOM の保険
    const std::string t = Trim(line);
    if (t.size() >= 3 && t[0] == '\xEF' && t[1] == '\xBB' && t[2] == '\xBF') return t.substr(3);
    return t;
}

// ヘッダ用 ASCII→wide 変換。非 ASCII バイト混入時は空を返し「キー不在」扱いにする
// （不正な Authorization ヘッダを送って無言の 401 になるのを防ぐ）。
std::wstring AsciiToW(const std::string& s) {
    for (const unsigned char c : s) {
        if (c > 0x7F || c < 0x21) return {};  // 非 ASCII・制御文字・空白はキーとして不正
    }
    return std::wstring(s.begin(), s.end());
}

}  // namespace

OpenAiKanaKanjiConverter::OpenAiKanaKanjiConverter(std::string model, std::string reasoningEffort)
    : model_(std::move(model)), reasoningEffort_(std::move(reasoningEffort)) {
    const std::string key = ReadApiKey();
    apiKey_ = AsciiToW(key);
}

void OpenAiKanaKanjiConverter::Convert(
        RequestId id, const yoshinani::core::domain::ConversionInput& input,
        std::function<void(RequestId, ConversionResult)> onDone) {
    ConversionResult result;
    if (apiKey_.empty()) {  // キー不在 → 即フォールバック（通信しない）
        onDone(id, std::move(result));
        return;
    }

    const std::string in  = OneLine(U16ToU8(input.source));
    const std::string ctx = OneLine(U16ToU8(input.context));

    // A/B 実測（HANDOFF §4）で空白なし入力も解けたプロンプト（分かち書き推測の指示入り）。
    // 継続モード: 直前の確定文を文脈節として渡す（同音異義・専門語の精度向上）。
    std::string prompt =
        "次の「ローマ字」を自然な日本語（漢字かな交じり）に変換してください。\n"
        "入力に空白がない場合は単語の区切り（分かち書き）を推測すること。\n"
        "ルール: 入力を漏れなく忠実に変換し、言い換え・要約・補足説明をしない。"
        "英語/専門用語/固有名詞は Latin 文字のまま残す。出力は変換後の日本語のみ。引用符や説明を付けない。\n\n"
        "例:\n入力: kyou ha openai de class wo tukutta\n出力: 今日はOpenAIでclassを作った\n"
        "入力: kyouhatenkigaii\n出力: 今日は天気がいい\n\n";
    if (!ctx.empty()) {
        prompt += "直前の文脈（語彙・話題の参考。出力には含めない）: " + ctx + "\n\n";
    }
    prompt += "入力: " + in + "\n出力:";

    nlohmann::json req;
    req["model"]            = model_;
    req["reasoning_effort"] = reasoningEffort_;
    req["seed"]             = 7;  // GPT-5 系は temperature 固定のため、seed で出力ブレを抑える（best-effort）
    req["messages"]         = nlohmann::json::array();
    req["messages"].push_back({{"role", "user"}, {"content", prompt}});

    // Authorization ヘッダ（シークレット込み）— ログ・例外へ出さない。
    const std::wstring headers = L"Authorization: Bearer " + apiKey_ + L"\r\n";

    // reasoning 系は思考時間が読めないため receive は 120s（失敗は生ローマ字フォールバック）。
    std::string resp;
    const bool ok = PostJson(L"api.openai.com", 443, /*secure=*/true,
                             L"/v1/chat/completions", headers, req.dump(), resp,
                             /*receiveTimeoutMs=*/120000);
    if (ok) {
        try {
            auto j = nlohmann::json::parse(resp);
            if (j.contains("choices") && !j["choices"].empty() &&
                j["choices"][0].contains("message") &&
                j["choices"][0]["message"].contains("content")) {
                result.text = U8ToU16(Trim(j["choices"][0]["message"]["content"].get<std::string>()));
                result.ok   = !result.text.empty();
            }
        } catch (...) {
            // 不正レスポンスは ok=false のまま（フォールバック）。
        }
    }
    onDone(id, std::move(result));
}

}  // namespace yoshinani::ipc
