// よしなに (Yoshinani) tray — 設定ウィンドウ（タブ付き GUI）。
//
// 役割: タスクトレイの「設定を開く」から起動される唯一の GUI ウィンドウ。
//   タブ:
//     1) Keymap   — triggerKeys / modeToggleKeys（確定トリガー・モード切替）
//     2) General  — settings.json のパス表示、JSON/フォルダを開く
//     3) Model    — バックエンド（OpenAI / Ollama）連動でモデル・effort を切替
//
// 設定ファイル: %APPDATA%\yoshinani\settings.json（書き込みは原子置換）。
//   TIP は次の Activate 時に読み直すため、即時反映は IME OFF→ON。
//
// モーダルレス。多重起動防止のため Show() は既存ウィンドウを最前面化する。
#pragma once

#include <windows.h>

namespace yoshinani::tray {

class SettingsWindow {
public:
    // ウィンドウを表示する。既に開いていれば SetForegroundWindow で前面化のみ。
    // 初回呼び出しでウィンドウクラス登録・コモンコントロール初期化が走る。
    static void Show(HINSTANCE hInst);

    // 現在開いているウィンドウのハンドル（無ければ nullptr）。
    // main.cpp のメッセージループが IsDialogMessageW に渡すために使う
    // （EDIT/Combo/Radio 間の Tab キー移動を有効化する）。
    static HWND ActiveHwnd();
};

}  // namespace yoshinani::tray
