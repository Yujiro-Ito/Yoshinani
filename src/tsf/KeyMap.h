// 設定のキー名 → Windows 仮想キーコード(VK) の対応（infra の責務）。
// core 側はポータブルなキー名で設定を持ち、ここで Windows の VK に変換する。
#pragma once
#include "Globals.h"
#include <optional>
#include <string>

// 既知のキー名なら対応する VK を返す。未知なら nullopt。
// 対応表を増やせば設定で指定できるトリガーキーが増える。
std::optional<WPARAM> KeyNameToVk(const std::string& name);
