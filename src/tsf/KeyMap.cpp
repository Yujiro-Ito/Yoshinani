// キー名 → VK 対応表。
#include "KeyMap.h"

std::optional<WPARAM> KeyNameToVk(const std::string& name) {
    if (name == "Space")  return static_cast<WPARAM>(VK_SPACE);
    if (name == "Period") return static_cast<WPARAM>(VK_OEM_PERIOD);  // 。
    if (name == "Comma")  return static_cast<WPARAM>(VK_OEM_COMMA);   // 、
    if (name == "Tab")    return static_cast<WPARAM>(VK_TAB);
    if (name == "Enter")  return static_cast<WPARAM>(VK_RETURN);
    return std::nullopt;
}
