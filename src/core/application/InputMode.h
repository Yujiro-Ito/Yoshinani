// 1-D 入力モード — application（OS非依存・テスト対象）。
// 変換モード（preedit に溜めて Tab 変換）⇄ 直接入力モード（全キー素通し＝コーディング用）。
// TSF 側はこの状態を GUID_COMPARTMENT_KEYBOARD_OPENCLOSE と同期させる
// （open=変換 / close=直接。言語バーやアプリからの切替にも追従する）。
#pragma once

namespace yoshinani::core::application {

enum class InputMode {
    Conversion,  // 変換モード（既定。IME がキーを食う）
    Direct,      // 直接入力モード（切替キー以外すべて素通し）
};

class InputModeState {
public:
    InputMode Get() const noexcept { return mode_; }
    bool IsDirect() const noexcept { return mode_ == InputMode::Direct; }

    void Set(InputMode m) noexcept { mode_ = m; }
    InputMode Toggle() noexcept {
        mode_ = (mode_ == InputMode::Conversion) ? InputMode::Direct : InputMode::Conversion;
        return mode_;
    }

private:
    InputMode mode_ = InputMode::Conversion;  // 既定は変換（IME を選んだ意思を尊重・1-D 確定事項）
};

}  // namespace yoshinani::core::application
