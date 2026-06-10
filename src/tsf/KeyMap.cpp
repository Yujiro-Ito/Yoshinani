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

wchar_t VkToChar(WPARAM vk, LPARAM lParam) {
    BYTE state[256];
    if (!GetKeyboardState(state)) return 0;

    // キーシンクは通常アプリの UI スレッドで呼ばれるが、念のためフォーカス先の
    // スレッドのレイアウトを引く（マルチスレッド構成・レイアウト切替環境対策）。
    const HWND hwndFocus = GetFocus();
    const DWORD tid = hwndFocus ? GetWindowThreadProcessId(hwndFocus, nullptr) : 0;
    const HKL hkl = GetKeyboardLayout(tid);

    // lParam の bits 16-23 がスキャンコード。UWP 系ホストは lParam=0 で呼ぶことが
    // あるため、その場合は VK から逆引きする（0 のままだと文字が取れず R2 が死ぬ）。
    UINT scanCode = (static_cast<UINT>(lParam) >> 16) & 0xFF;
    if (scanCode == 0) scanCode = MapVirtualKeyEx(static_cast<UINT>(vk), MAPVK_VK_TO_VSC, hkl);

    // フラグ 0x4 (Win10 1607+): カーネルのキーボード状態を変更しない。
    // OnTestKeyDown → OnKeyDown で 2 回呼ばれても dead key 状態を壊さないために必須。
    wchar_t buf[4] = {};
    const int n = ToUnicodeEx(static_cast<UINT>(vk), scanCode, state, buf, 4, 0x4, hkl);
    if (n != 1) return 0;  // 0=文字なし / 負=dead key / 2+=合成は扱わない

    const wchar_t ch = buf[0];
    if (ch < 0x20 || ch == 0x7F) return 0;  // 制御文字（Tab/CR/Esc 等）・DEL は文字扱いしない
    return ch;
}
