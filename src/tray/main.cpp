// yoshinani-tray.exe — 通知領域の常駐アプリ。
//
// 役割（最小）:
//   - 通知領域にアイコンを置く（右クリックで「設定を開く」「終了」だけのメニュー）
//   - 「設定を開く」で SettingsWindow を表示（Keymap / General / Model タブ）
//
// 設定の編集 UI は SettingsWindow に集約。ここはトレイの常駐生命線だけ持つ。

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include "SettingsWindow.h"
#include "resource.h"

namespace {

constexpr UINT  WM_APP_TRAY = WM_APP + 1;
constexpr UINT  TRAY_UID    = 1;
constexpr wchar_t kWindowClass[] = L"YoshinaniTrayWnd";
constexpr wchar_t kWindowTitle[] = L"よしなに (Tray)";
constexpr wchar_t kTooltip[]     = L"よしなに — クリックで設定";
constexpr wchar_t kMutexName[]   = L"Local\\YoshinaniTrayInstance";

constexpr UINT ID_OPEN_SETTINGS = 1000;
constexpr UINT ID_QUIT          = 1999;

// 通知領域のターゲット DPI に合わせた小アイコン（既定 16x16）を取る。
HICON LoadTrayIcon(HINSTANCE hInst) {
    const int cx = GetSystemMetrics(SM_CXSMICON);
    const int cy = GetSystemMetrics(SM_CYSMICON);
    HICON hIcon = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(IDI_TRAY),
                                                IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR));
    if (!hIcon) hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    return hIcon;
}

bool AddTrayIcon(HWND hWnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = hWnd;
    nid.uID              = TRAY_UID;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon            = LoadTrayIcon(reinterpret_cast<HINSTANCE>(
        GetWindowLongPtrW(hWnd, GWLP_HINSTANCE)));
    lstrcpynW(nid.szTip, kTooltip, ARRAYSIZE(nid.szTip));
    return Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
}

void RemoveTrayIcon(HWND hWnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = hWnd;
    nid.uID    = TRAY_UID;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void ShowContextMenu(HWND hWnd, POINT pt) {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;
    AppendMenuW(hMenu, MF_STRING, ID_OPEN_SETTINGS, L"設定を開く");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_QUIT, L"終了");

    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                   pt.x, pt.y, 0, hWnd, nullptr);
    PostMessage(hWnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            if (!AddTrayIcon(hWnd)) return -1;
            return 0;
        case WM_APP_TRAY:
            switch (LOWORD(lParam)) {
                case WM_LBUTTONUP: {
                    // 左クリック1発で設定を開く（visual thinker の最短導線）。
                    HINSTANCE hi = reinterpret_cast<HINSTANCE>(
                        GetWindowLongPtrW(hWnd, GWLP_HINSTANCE));
                    yoshinani::tray::SettingsWindow::Show(hi);
                    return 0;
                }
                case WM_RBUTTONUP:
                case WM_CONTEXTMENU: {
                    POINT pt;
                    GetCursorPos(&pt);
                    ShowContextMenu(hWnd, pt);
                    return 0;
                }
                default:
                    return 0;
            }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_OPEN_SETTINGS: {
                    HINSTANCE hi = reinterpret_cast<HINSTANCE>(
                        GetWindowLongPtrW(hWnd, GWLP_HINSTANCE));
                    yoshinani::tray::SettingsWindow::Show(hi);
                    return 0;
                }
                case ID_QUIT:
                    DestroyWindow(hWnd);
                    return 0;
            }
            return 0;
        case WM_DESTROY:
            RemoveTrayIcon(hWnd);
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

}  // namespace

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int) {
    HANDLE hMutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 0;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = kWindowClass;
    wc.hIcon         = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_TRAY),
                                                     IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    if (!RegisterClassW(&wc)) return 1;

    HWND hWnd = CreateWindowExW(0, kWindowClass, kWindowTitle, 0,
                                0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, hInstance, nullptr);
    if (!hWnd) return 1;

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // 設定ウィンドウが開いているときだけ Tab キー移動を有効化（IsDialogMessageW）。
        // GetActiveWindow() はトレイのメッセージ専用ウィンドウだと nullptr を返すため使えない。
        HWND hSettings = yoshinani::tray::SettingsWindow::ActiveHwnd();
        if (!hSettings || !IsDialogMessageW(hSettings, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    UnregisterClassW(kWindowClass, hInstance);
    if (hMutex) CloseHandle(hMutex);
    return static_cast<int>(msg.wParam);
}
