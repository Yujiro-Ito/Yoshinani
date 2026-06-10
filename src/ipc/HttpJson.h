// ipc 内部共有: WinHTTP の同期 POST（JSON）。Ollama(HTTP) / OpenAI(HTTPS) 共通の往復部。
#pragma once
#include <string>

namespace yoshinani::ipc {

// host へ body を POST し、200 のときだけ outBody を埋めて true を返す。
//   secure=true で HTTPS(443 等)。extraHeaders は "Name: value\r\n" 連結（無ければ空。
//   Content-Type: application/json は常に付与）。認証ヘッダを渡す場合があるため、
//   呼び出し側はこの文字列をログ・例外メッセージへ出さないこと。
//   receiveTimeoutMs はバックエンド特性（冷起動・推論時間）に合わせて指定。
bool PostJson(const std::wstring& host, int port, bool secure,
              const std::wstring& path, const std::wstring& extraHeaders,
              const std::string& body, std::string& outBody, int receiveTimeoutMs = 60000);

}  // namespace yoshinani::ipc
