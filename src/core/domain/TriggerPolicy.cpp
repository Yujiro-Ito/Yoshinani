// 1-C トリガー確定 — 判定表の実装。
#include "domain/TriggerPolicy.h"

namespace yoshinani::core::domain {

InputAction Decide(KeyKind kind, bool preeditEmpty) noexcept {
    switch (kind) {
        case KeyKind::Character:
            return InputAction::Append;
        case KeyKind::Kuten:
        case KeyKind::Touten:
            return preeditEmpty ? InputAction::PassThrough : InputAction::CommitWithPunct;
        case KeyKind::ShiftTab:
            return preeditEmpty ? InputAction::PassThrough : InputAction::Commit;
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
