// ipc 内部共有: WinHTTP の同期 POST（JSON）実装。
#include "HttpJson.h"

#include <windows.h>
#include <winhttp.h>

namespace yoshinani::ipc {

bool PostJson(const std::wstring& host, int port, bool secure,
              const std::wstring& path, const std::wstring& extraHeaders,
              const std::string& body, std::string& outBody, int receiveTimeoutMs) {
    if (body.size() > MAXDWORD) return false;  // WinHTTP の長さ引数は DWORD。実用上到達しないが防御。
    std::string resp;
    DWORD status = 0;

    // AUTOMATIC_PROXY: 企業プロキシ/VPN 環境でも OS 設定を継承して外へ出られるように
    // （localhost はプロキシをバイパスするため Ollama には影響しない）。
    HINTERNET hSession = WinHttpOpen(L"yoshinani/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (hSession) {
        // resolve/connect/send/receive のタイムアウト(ms)。receive は呼び出し側が
        // バックエンド特性（冷起動・推論時間）に応じて指定する。
        WinHttpSetTimeouts(hSession, 5000, 5000, 10000, receiveTimeoutMs);
        HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(),
                                            static_cast<INTERNET_PORT>(port), 0);
        if (hConnect) {
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(), nullptr,
                                                    WINHTTP_NO_REFERER,
                                                    WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                    secure ? WINHTTP_FLAG_SECURE : 0);
            if (hRequest) {
                std::wstring headers = L"Content-Type: application/json\r\n" + extraHeaders;
                BOOL sent = WinHttpSendRequest(
                    hRequest, headers.c_str(), static_cast<DWORD>(-1),
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
    outBody = std::move(resp);
    return true;
}

}  // namespace yoshinani::ipc
