// ipc 内部共有: UTF-16/UTF-8 変換と前後空白トリム（Windows 専用・ipc 層に閉じる）。
#pragma once
#include <windows.h>

#include <string>
#include <string_view>

namespace yoshinani::ipc {

// UTF-16 変換は char16_t を wchar_t* として渡す前提（Windows/MSVC では 2byte 同一表現）。
static_assert(sizeof(wchar_t) == sizeof(char16_t),
              "UTF-16 変換は wchar_t==char16_t(2byte) を前提とする（Windows/MSVC）");

inline std::string U16ToU8(std::u16string_view s) {
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

inline std::u16string U8ToU16(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return std::u16string(reinterpret_cast<const char16_t*>(w.data()), w.size());
}

// 前後の ASCII 空白・改行を落とす（モデルが付ける余計な空白/改行の保険）。
inline std::string Trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

}  // namespace yoshinani::ipc
