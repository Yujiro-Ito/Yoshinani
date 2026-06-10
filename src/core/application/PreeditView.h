// 4-A/4-B preedit 連結表示 — application（OS非依存・テスト対象）。
// B方式（§6.5）: 見た目は 1 本の preedit だが、内部は「変換待ちセグメント列 + 打鍵中」。
//   [A:変換中][B:変換中][C:打鍵中]
// この連結文字列と「変換中区間の長さ」を算出する純関数。TSF 層はこの値を
// SetText と表示属性（実線/点線下線・4-B）の適用にそのまま使う。
#pragma once
#include <string>

#include "application/ConversionQueue.h"

namespace yoshinani::core::application {

struct PreeditView {
    std::u16string text;          // 表示文字列（変換待ちソース連結 + 打鍵中）
    std::size_t convertingLen{};  // 先頭からの「変換中」区間長（残りが「入力中」区間）
};

// 変換待ち(Pending)はソース（ローマ字）、終端(Done/Failed)は result（確定文／改行・生確定
// セグメントを含む）を表示する。前段が変換中で確定待ちのセグメントも「打った位置」に正しく出る。
inline PreeditView BuildPreeditView(const ConversionQueue& queue, const std::u16string& typing) {
    PreeditView v;
    for (const auto& r : queue.Items()) {
        // 確定ロジックと一致させる: Done は result、Pending/Failed は source（失敗は生ローマ字）。
        v.text += (r.state == ConvState::Done) ? r.result : r.source;
    }
    v.convertingLen = v.text.size();
    v.text += typing;
    return v;
}

}  // namespace yoshinani::core::application
