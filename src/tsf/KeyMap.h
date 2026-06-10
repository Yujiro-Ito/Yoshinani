// 設定のキー名 → Windows 仮想キーコード(VK) の対応（infra の責務）。
// core 側はポータブルなキー名で設定を持ち、ここで Windows の VK に変換する。
#pragma once
#include "Globals.h"
#include <optional>
#include <string>

// 既知のキー名なら対応する VK を返す。未知なら nullopt。
// 対応表を増やせば設定で指定できるトリガーキーが増える。
std::optional<WPARAM> KeyNameToVk(const std::string& name);

// VK → 設定で使うキー名の逆引き。未対応 VK は nullopt（トレイ UI のキーキャプチャで使用）。
// 半角/全角の VK 揺れ（VK_KANJI / VK_OEM_AUTO / VK_OEM_ENLW）は全て "Kanji" に正規化する。
std::optional<std::string> VkToKeyName(WPARAM vk);

// 打鍵を現在のキーボード状態（Shift/CapsLock）とレイアウトで文字に変換する（R1/R2）。
// 印字可能文字（空白を含む）なら値を、制御キー・dead key・非印字なら 0 を返す。
// lParam は WM_KEYDOWN のもの（スキャンコードを bits 16-23 から取る）。
wchar_t VkToChar(WPARAM vk, LPARAM lParam);
