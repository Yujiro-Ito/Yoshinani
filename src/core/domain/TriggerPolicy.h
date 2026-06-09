// 1-C トリガー確定 — domain（純粋ロジック・テスト対象）
// 正規化済みキー種別から「IME が取るべきアクション」を返す。
// TSF/VK コードはここに持ち込まない（infra が正規化して渡す）。
#pragma once

namespace yoshinani::core::domain {

// infra が VK + 修飾キーから正規化して渡すキー種別。
//   Trigger は「確定の合図」。どのキーを Trigger とみなすかは infra 側の
//   トリガーキー一覧（設定）で決める（既定 Tab。後から追加可能）。
enum class KeyKind {
    Character,   // 通常文字（preedit に足す。文字値は別途渡す）
    Trigger,     // 確定トリガー（既定 Tab）
    Space,       // 区切りの空白。preedit 中のみ足す（空のときは素通し）
    Backspace,
    Escape,
    Other,       // 自分は扱わない
};

// IME が取るべきアクション。
enum class InputAction {
    Append,       // preedit に文字を足す
    Commit,       // 確定して手放す（トリガーキー自体は消費）
    DeleteLast,   // 末尾 1 文字削除
    Cancel,       // 取り消して手放す（Esc）
    PassThrough,  // 食わずにアプリへ
};

// 判定（1-C の判定表）。preeditEmpty = 現在 preedit が空か。
//   ※ 将来「句読点ごと確定（。、を出力に含める）」を足すときは
//      CommitWithPunct を再導入し、Trigger の種類で分岐する想定（[[phase1-trigger-commit]]）。
InputAction Decide(KeyKind kind, bool preeditEmpty) noexcept;

}  // namespace yoshinani::core::domain
