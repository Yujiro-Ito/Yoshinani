// 1-C トリガー確定 — 判定表の実装。
#include "domain/TriggerPolicy.h"

namespace yoshinani::core::domain {

InputAction Decide(KeyKind kind, bool preeditEmpty) noexcept {
    switch (kind) {
        case KeyKind::Character:
            return InputAction::Append;
        case KeyKind::Trigger:
            return preeditEmpty ? InputAction::PassThrough : InputAction::Commit;
        case KeyKind::Enter:
            // 生確定: 入力中なら preedit を変換せずそのまま確定。空なら通常の改行として素通し。
            return preeditEmpty ? InputAction::PassThrough : InputAction::CommitRaw;
        case KeyKind::Space:
            // 区切り空白: 入力中なら preedit に足す。空のときは通常の空白として素通し。
            return preeditEmpty ? InputAction::PassThrough : InputAction::Append;
        case KeyKind::Backspace:
            return preeditEmpty ? InputAction::PassThrough : InputAction::DeleteLast;
        case KeyKind::Escape:
            return preeditEmpty ? InputAction::PassThrough : InputAction::Cancel;
        case KeyKind::Other:
        default:
            return InputAction::PassThrough;
    }
}

}  // namespace yoshinani::core::domain
