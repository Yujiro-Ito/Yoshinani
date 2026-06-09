// 3-A 実装（v1 stand-in）: Ollama(Gemma) を WinHTTP で叩いて romaji→日本語に変換する。
// 失敗・デーモン不在時は ok=false を返し、呼び出し側(3-E)が生入力のまま確定する。
#include "OllamaKanaKanjiConverter.h"

#include <windows.h>
#include <winhttp.h>

#include <nlohmann/json.hpp>

#include <utility>

namespace yoshinani::ipc {

using yoshinani::core::domain::ConversionResult;
using yoshinani::core::domain::RequestId;

namespace {

// UTF-16 変換は char16_t を wchar_t* として渡す前提（Windows/MSVC では 2byte 同一表現）。
static_assert(sizeof(wchar_t) == sizeof(char16_t),
              "UTF-16 変換は wchar_t==char16_t(2byte) を前提とする（Windows/MSVC）");

std::string U16ToU8(std::u16string_view s) {
    if (s.empty()) return {};
    const wchar_t* w = reinterpret_cast<const wchar_t*>(s.data());
    int n = WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(s.size()),
                                nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(s.size()),
                        out.data(), n, nullptr, nullptr);
    return out;
}

std::u16string U8ToU16(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return std::u16string(reinterpret_cast<const char16_t*>(w.data()), w.size());
}

// 前後の ASCII 空白・改行を落とす（モデルが付ける余計な空白/改行の保険）。
std::string Trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Ollama /api/chat に同期 POST し、message.content を outContent に返す。
//   戻り値 false: 接続失敗 / 非200 / レスポンス不正（呼び出し側でフォールバック）。
bool CallOllama(const std::wstring& host, int port, const std::string& body,
                std::string& outContent) {
    if (body.size() > MAXDWORD) return false;  // WinHTTP の長さ引数は DWORD。実用上到達しないが防御。
    std::string resp;
    DWORD status = 0;

    HINTERNET hSession = WinHttpOpen(L"yoshinani/0.1", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (hSession) {
        // resolve/connect/send/receive のタイムアウト(ms)。冷起動を考え receive は長め。
        WinHttpSetTimeouts(hSession, 5000, 5000, 10000, 60000);
        HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(),
                                            static_cast<INTERNET_PORT>(port), 0);
        if (hConnect) {
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/api/chat", nullptr,
                                                    WINHTTP_NO_REFERER,
                                                    WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
            if (hRequest) {
                const wchar_t* headers = L"Content-Type: application/json\r\n";
                BOOL sent = WinHttpSendRequest(
                    hRequest, headers, static_cast<DWORD>(-1),
                    const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
                    static_cast<DWORD>(body.size()), 0);
                if (sent && WinHttpReceiveResponse(hRequest, nullptr)) {
                    DWORD sz = sizeof(status);
                    WinHttpQueryHeaders(hRequest,
                                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz,
                                        WINHTTP_NO_HEADER_INDEX);
                    DWORD avail = 0;
                    for (;;) {
                        if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0) break;
                        std::string chunk(avail, '\0');
                        DWORD read = 0;
                        if (!WinHttpReadData(hRequest, chunk.data(), avail, &read)) break;
                        chunk.resize(read);
                        resp += chunk;
                    }
                }
                WinHttpCloseHandle(hRequest);
            }
            WinHttpCloseHandle(hConnect);
        }
        WinHttpCloseHandle(hSession);
    }

    if (status != 200 || resp.empty()) return false;
    try {
        auto j = nlohmann::json::parse(resp);
        if (j.contains("message") && j["message"].contains("content")) {
            outContent = j["message"]["content"].get<std::string>();
            return true;
        }
    } catch (...) {
        return false;
    }
    return false;
}

}  // namespace

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

    std::string content;
    const bool ok = CallOllama(host_, port_, req.dump(), content);

    ConversionResult result;
    if (ok) {
        result.text = U8ToU16(Trim(content));
        result.ok   = !result.text.empty();
    } else {
        result.ok = false;
    }
    onDone(id, std::move(result));
}

}  // namespace yoshinani::ipc
