// 1-C トリガー確定 — domain（純粋ロジック・テスト対象）
// 正規化済みキー種別から「IME が取るべきアクション」を返す。
// TSF/VK コードはここに持ち込まない（infra が正規化して渡す）。
#pragma once

namespace yoshinani::core::domain {

// infra が VK + 修飾キーから正規化して渡すキー種別。
enum class KeyKind {
    Character,   // 通常文字（英字など。文字値は別途渡す）
    Kuten,       // 。
    Touten,      // 、
    ShiftTab,    // 手動トリガー
    Backspace,
    Escape,
    Other,       // 自分は扱わない
};

// IME が取るべきアクション。
enum class InputAction {
    Append,           // preedit に文字を足す
    CommitWithPunct,  // 句読点ごと確定して手放す（。、）
    Commit,           // 確定して手放す（Shift+Tab）
    DeleteLast,       // 末尾 1 文字削除
    Cancel,           // 取り消して手放す（Esc）
    PassThrough,      // 食わずにアプリへ
};

// 判定（1-C の判定表）。preeditEmpty = 現在 preedit が空か。
InputAction Decide(KeyKind kind, bool preeditEmpty) noexcept;

}  // namespace yoshinani::core::domain
