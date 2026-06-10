// 継続モード（文脈チェイン）— application（OS非依存・テスト対象）。
// 直前に確定した文を数件保持し、変換プロンプトの文脈節として渡す（LazyJP 相当）。
// 「だけん→打鍵」のような同音異義・専門語が、前文の話題から当たりやすくなる。
//
// ユーザー辞書と違い登録・同期の手間がない代わりに、効くのは「直前の文脈」だけ。
#pragma once
#include <deque>
#include <string>

namespace yoshinani::core::application {

class ContextHistory {
public:
    // maxEntries 件まで保持し、Tail は新しい順を保ったまま末尾 maxChars に切り詰める。
    explicit ContextHistory(std::size_t maxEntries = 3, std::size_t maxChars = 240)
        : maxEntries_(maxEntries), maxChars_(maxChars) {}

    // 確定した文を追加（空文字は無視）。古いものから捨てる。
    void Push(const std::u16string& text) {
        if (text.empty()) return;
        entries_.push_back(text);
        while (entries_.size() > maxEntries_) entries_.pop_front();
    }

    // 文脈文字列（古い順に空白連結）。全体が maxChars を超える場合は先頭（古い側）を捨てる。
    std::u16string Tail() const {
        std::u16string joined;
        for (const auto& e : entries_) {
            if (!joined.empty()) joined += u' ';
            joined += e;
        }
        if (joined.size() > maxChars_) {
            joined.erase(0, joined.size() - maxChars_);
        }
        return joined;
    }

    void Clear() noexcept { entries_.clear(); }
    bool Empty() const noexcept { return entries_.empty(); }

private:
    std::size_t maxEntries_;
    std::size_t maxChars_;
    std::deque<std::u16string> entries_;
};

}  // namespace yoshinani::core::application
