// 1-B preedit — 溜め状態（application 層・OS非依存）。
// Step1 はローマ字をそのまま溜めるだけ。Step2 で romaji→kana を噛ませる差し替え点。
#pragma once
#include <string>

namespace yoshinani::core::application {

class InputSession {
public:
    void AppendChar(char16_t c) { preedit_.push_back(c); }
    void Backspace()            { if (!preedit_.empty()) preedit_.pop_back(); }
    void Clear()                { preedit_.clear(); }

    bool Empty() const noexcept { return preedit_.empty(); }

    // 現在の未確定表示文字列（UTF-16。TSF へはこのまま渡せる）。
    const std::u16string& Preedit() const noexcept { return preedit_; }

private:
    std::u16string preedit_;
};

}  // namespace yoshinani::core::application
