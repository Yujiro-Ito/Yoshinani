// yoshinani-tray.exe — 通知領域アイコンから設定を切り替える常駐 GUI。
//
// 役割（最小）:
//   - 通知領域にアイコンを置く（右クリックでメニュー）
//   - settings.json の converter（backend/model/reasoningEffort）を選んで書き換える
//   - settings.json をエディタで開く / 終了
//
// 設定ファイル: %APPDATA%\yoshinani\settings.json
//   TIP（yoshinani.dll）は次回 Activate 時に同じ場所を読み直す。
//   即時反映したい時は IME を OFF→ON で切替（メニューに注記）。
//
// 重い処理は持たない。コアロジック（ParseSettings/SerializeSettings）は core を共有。

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "application/Settings.h"

using yoshinani::core::application::ConverterSettings;
using yoshinani::core::application::ParseSettings;
using yoshinani::core::application::SerializeSettings;
using yoshinani::core::application::Settings;

namespace {

constexpr UINT  WM_APP_TRAY = WM_APP + 1;
constexpr UINT  TRAY_UID    = 1;
constexpr wchar_t kWindowClass[]  = L"YoshinaniTrayWnd";
constexpr wchar_t kWindowTitle[]  = L"よしなに (Tray)";
constexpr wchar_t kTooltip[]      = L"よしなに — クリックで設定";
constexpr wchar_t kMutexName[]    = L"Local\\YoshinaniTrayInstance";

// メニュー ID 帯（衝突回避のため帯で分ける）。
constexpr UINT ID_BACKEND_BASE  = 1000;  // backend candidates
constexpr UINT ID_MODEL_BASE    = 1100;  // model candidates
constexpr UINT ID_EFFORT_BASE   = 1200;  // reasoningEffort candidates
constexpr UINT ID_OPEN_SETTINGS = 1900;
constexpr UINT ID_OPEN_FOLDER   = 1901;
constexpr UINT ID_QUIT          = 1999;

struct Candidate {
    const wchar_t* label;  // メニュー表示
    const char*    value;  // settings.json に書く値（"" は「バックエンド既定」を意味）
};

// 切替可能な値のホワイトリスト（HANDOFF §5 の A/B 実測に基づく採用候補）。
const std::vector<Candidate> kBackends = {
    {L"OpenAI (クラウド)",    "openai"},
    {L"Ollama (ローカル)",    "ollama"},
};
const std::vector<Candidate> kOpenAiModels = {
    {L"gpt-5.4-nano",  "gpt-5.4-nano"},
    {L"gpt-5.4-mini (採用)", "gpt-5.4-mini"},
    {L"gpt-5.4 (フル)",      "gpt-5.4"},
};
const std::vector<Candidate> kOllamaModels = {
    {L"gemma4:e2b-it-qat",       "gemma4:e2b-it-qat"},
    {L"gemma4:e4b-it-qat (採用)", "gemma4:e4b-it-qat"},
};
const std::vector<Candidate> kEfforts = {
    {L"none",          "none"},
    {L"low (採用)",    "low"},
    {L"medium",        "medium"},
    {L"high",          "high"},
    {L"xhigh",         "xhigh"},
};

// ---- ファイル I/O ------------------------------------------------------------

std::wstring SettingsDir() {
    PWSTR roaming = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) return {};
    std::wstring p(roaming);
    CoTaskMemFree(roaming);
    p += L"\\yoshinani";
    return p;
}

std::wstring SettingsPath() {
    auto dir = SettingsDir();
    return dir.empty() ? std::wstring{} : (dir + L"\\settings.json");
}

std::string ReadAllText(const std::wstring& path) {
    std::ifstream f(path.c_str());
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// tmp に書いて MoveFileEx で置換（更新中の TIP 読み込みでも壊れた JSON を見せない）。
bool WriteAtomic(const std::wstring& path, const std::string& text) {
    const std::wstring tmp = path + L".tmp";
    {
        std::ofstream f(tmp.c_str(), std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        f.close();             // 明示 close でフラッシュ
        if (!f) {              // close 後の fail bit を確認（書き込み/フラッシュ失敗の検出）
            DeleteFileW(tmp.c_str());
            return false;
        }
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

// 現在の設定を読む（無ければ既定）。
Settings LoadCurrent() {
    const auto path = SettingsPath();
    if (path.empty()) return Settings{};
    const auto text = ReadAllText(path);
    if (text.empty()) return Settings{};
    return ParseSettings(text);
}

// 設定を書き戻す（ディレクトリ作成込み）。
bool SaveCurrent(const Settings& s) {
    const auto dir = SettingsDir();
    if (dir.empty()) return false;
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);  // 既に在れば 183(ERROR_ALREADY_EXISTS)
    const auto path = dir + L"\\settings.json";
    return WriteAtomic(path, SerializeSettings(s));
}

// ---- 通知領域アイコン --------------------------------------------------------

bool AddTrayIcon(HWND hWnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = hWnd;
    nid.uID              = TRAY_UID;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon            = LoadIcon(nullptr, IDI_APPLICATION);  // 仮アイコン（後でリソース化）
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

// ---- メニュー ----------------------------------------------------------------

void AppendCandidates(HMENU hSub, UINT idBase, const std::vector<Candidate>& items,
                      const std::string& current) {
    for (size_t i = 0; i < items.size(); ++i) {
        const bool checked = (current == items[i].value);
        AppendMenuW(hSub, MF_STRING | (checked ? MF_CHECKED : MF_UNCHECKED),
                    idBase + static_cast<UINT>(i), items[i].label);
    }
}

void ShowContextMenu(HWND hWnd, POINT pt) {
    const Settings s = LoadCurrent();
    const ConverterSettings& c = s.converter;

    HMENU hMenu    = CreatePopupMenu();
    HMENU hBackend = CreatePopupMenu();
    HMENU hModel   = CreatePopupMenu();
    HMENU hEffort  = CreatePopupMenu();

    AppendCandidates(hBackend, ID_BACKEND_BASE, kBackends, c.backend);

    // モデル候補はバックエンドで切り替える（混在しても良いが UI として狭めた方が事故らない）。
    const auto& models = (c.backend == "ollama") ? kOllamaModels : kOpenAiModels;
    AppendCandidates(hModel, ID_MODEL_BASE, models, c.model);
    // "model" 未指定（空文字＝バックエンド既定）も明示できるよう先頭に項目を足す。
    InsertMenuW(hModel, 0, MF_BYPOSITION | MF_STRING |
                (c.model.empty() ? MF_CHECKED : MF_UNCHECKED),
                ID_MODEL_BASE + static_cast<UINT>(models.size()),
                L"(バックエンド既定)");

    AppendCandidates(hEffort, ID_EFFORT_BASE, kEfforts, c.reasoningEffort);

    AppendMenuW(hMenu, MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(hBackend),
                L"バックエンド");
    AppendMenuW(hMenu, MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(hModel),
                L"モデル");
    AppendMenuW(hMenu, MF_STRING | MF_POPUP | (c.backend == "ollama" ? MF_GRAYED : 0),
                reinterpret_cast<UINT_PTR>(hEffort),
                L"reasoningEffort (OpenAI のみ)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_OPEN_SETTINGS, L"settings.json を開く");
    AppendMenuW(hMenu, MF_STRING, ID_OPEN_FOLDER,   L"設定フォルダを開く");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0,
                L"※変更は IME OFF→ON で反映");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_QUIT, L"終了");

    // TrackPopupMenu が前面でないとフォーカスが効かない既知挙動。
    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                   pt.x, pt.y, 0, hWnd, nullptr);
    PostMessage(hWnd, WM_NULL, 0, 0);  // メニュー閉じ後のフリーズ対策（古典）

    DestroyMenu(hMenu);  // サブメニューも芋づるで破棄される
}

// 候補配列を ID 帯から逆引きして value を取り出す（範囲外は nullptr）。
const char* PickValue(UINT id, UINT base, const std::vector<Candidate>& items) {
    if (id < base) return nullptr;
    const size_t idx = static_cast<size_t>(id - base);
    if (idx >= items.size()) return nullptr;
    return items[idx].value;
}

void HandleCommand(HWND hWnd, UINT id) {
    if (id == ID_QUIT) {
        DestroyWindow(hWnd);
        return;
    }
    if (id == ID_OPEN_SETTINGS) {
        const auto path = SettingsPath();
        if (!path.empty()) {
            // ファイルが無い場合は既定で作っておく（編集後の存在前提を満たす）。
            if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
                SaveCurrent(LoadCurrent());
            }
            ShellExecuteW(hWnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        return;
    }
    if (id == ID_OPEN_FOLDER) {
        const auto dir = SettingsDir();
        if (!dir.empty()) {
            SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
            ShellExecuteW(hWnd, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        return;
    }

    Settings s = LoadCurrent();
    bool changed = false;

    if (const char* backendVal = PickValue(id, ID_BACKEND_BASE, kBackends)) {
        if (s.converter.backend != backendVal) {
            s.converter.backend = backendVal;
            // バックエンドを切り替えた瞬間、モデルが他方の値（例: OpenAI モデル名なのに ollama）
            // のまま残ると変換が崩れる。空にして「バックエンド既定」へ戻す（安全側）。
            s.converter.model.clear();
            changed = true;
        }
    } else if (id >= ID_MODEL_BASE && id < ID_EFFORT_BASE) {
        const auto& models = (s.converter.backend == "ollama") ? kOllamaModels : kOpenAiModels;
        const UINT idx = id - ID_MODEL_BASE;
        std::string next;
        if (idx < models.size()) {
            next = models[idx].value;
        } else if (idx == models.size()) {
            next.clear();  // (バックエンド既定)
        } else {
            return;
        }
        if (s.converter.model != next) {
            s.converter.model = next;
            changed = true;
        }
    } else if (const char* effortVal = PickValue(id, ID_EFFORT_BASE, kEfforts)) {
        if (s.converter.reasoningEffort != effortVal) {
            s.converter.reasoningEffort = effortVal;
            changed = true;
        }
    }

    if (changed && !SaveCurrent(s)) {
        MessageBoxW(hWnd,
                    L"settings.json の書き込みに失敗しました。\n"
                    L"%APPDATA%\\yoshinani への書き込み権限を確認してください。",
                    L"よしなに (Tray)",
                    MB_OK | MB_ICONERROR);
    }
}

// ---- ウィンドウプロシージャ --------------------------------------------------

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            if (!AddTrayIcon(hWnd)) return -1;
            return 0;
        case WM_APP_TRAY:
            switch (LOWORD(lParam)) {
                case WM_RBUTTONUP:
                case WM_LBUTTONUP:
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
            HandleCommand(hWnd, LOWORD(wParam));
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
    // 二重起動防止（既に居れば即終了）。
    HANDLE hMutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);  // 同名 Mutex への参照を解放してから終了
        return 0;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassW(&wc)) return 1;

    // メッセージ専用ウィンドウ（HWND_MESSAGE）でタスクバーに出さない。
    HWND hWnd = CreateWindowExW(0, kWindowClass, kWindowTitle, 0,
                                0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, hInstance, nullptr);
    if (!hWnd) return 1;

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnregisterClassW(kWindowClass, hInstance);
    if (hMutex) CloseHandle(hMutex);
    return static_cast<int>(msg.wParam);
}
