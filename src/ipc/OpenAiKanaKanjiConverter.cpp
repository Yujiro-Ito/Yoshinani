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

    // 指示部は英語（実測で約22%のトークン削減・精度は日本語指示と実質同等・2026-06-10）。
    // 例文と入力/出力は日本語のまま。空白なし推測・誤字修正・記号の日本語化・%...% 特別指示に対応。
    // 継続モード: 直前の確定文を文脈ヒントとして渡す（同音異義・専門語の精度向上）。
    std::string prompt =
        "Convert romaji to natural Japanese (kanji-kana). If there are no spaces, infer word boundaries.\n"
        "Translate faithfully; do not paraphrase or summarize. Only fix clear typos that would otherwise break the Japanese.\n"
        "Convert symbols ([] \"\" - ? ! etc.) to Japanese ones (「」『』ー、。！？) when they fit the context.\n"
        "Keep English, technical terms, and proper nouns in Latin script. Output only the converted text.\n"
        "Text wrapped in %...% is a one-time instruction for this conversion: obey it and do not output the instruction itself (e.g. %polite form% %translate to English%).\n\n"
        "kyou ha openai de class wo tukutta → 今日はOpenAIでclassを作った\n"
        "kyouhatenkigaii → 今日は天気がいい\n"
        "\"sushi\" wo tabeta! → 「sushi」を食べた！\n"
        "%teineigo nisite% asita iku → 明日行きます\n\n";
    if (!ctx.empty()) {
        prompt += "Context (hint only, do not output): " + ctx + "\n\n";
    }
    prompt += in + " →";

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
