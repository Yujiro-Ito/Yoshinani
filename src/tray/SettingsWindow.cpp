// よしなに (Yoshinani) tray — 設定ウィンドウ実装。
//
// 構成:
//   トップレベル CreateWindowExW で作る独立ウィンドウ。中身は SysTabControl32 と
//   タブごとの子コントロール群、下段に [OK][Cancel][Apply]。タブ切替は
//   子コントロールの Show/Hide で実装（タブごとに別ダイアログを作らない・最小）。
//
// 設計判断:
//   - ダイアログテンプレート（.rc）ではなく動的 CreateWindow で全コントロールを作る。
//     ロケール/フォントスケーリングが扱いやすい・1ファイルで完結する・後でコード上で
//     タブを追加しやすい、というメリットを取った。
//   - LayoutGrid 等は導入しない。座標は MetricsToPx で DLU 風に固定スケール。
//   - 編集 → Apply → 書き込み → 元状態更新。Cancel は破棄。OK は Apply 後に閉じる。

#include "SettingsWindow.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "application/Settings.h"
#include "resource.h"

using yoshinani::core::application::ConverterSettings;
using yoshinani::core::application::ParseSettings;
using yoshinani::core::application::SerializeSettings;
using yoshinani::core::application::Settings;

namespace yoshinani::tray {

namespace {

// ---- ファイル I/O（main.cpp と同じ規約・共有ヘッダにせず重複は許容）-----------

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

bool WriteAtomic(const std::wstring& path, const std::string& text) {
    const std::wstring tmp = path + L".tmp";
    {
        std::ofstream f(tmp.c_str(), std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        f.close();
        if (!f) {
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

bool EnsureSettingsDir() {
    const auto dir = SettingsDir();
    if (dir.empty()) return false;
    const int rc = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    // 既に存在 / 親も既存 — どちらもエラーとして返るが成功扱い。
    return rc == ERROR_SUCCESS || rc == ERROR_ALREADY_EXISTS || rc == ERROR_FILE_EXISTS;
}

// ---- 文字列変換 --------------------------------------------------------------

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                      nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n,
                        nullptr, nullptr);
    return s;
}

// カンマ区切り文字列 → 配列（前後空白を trim）。
std::vector<std::string> SplitCSV(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        size_t a = 0, b = cur.size();
        while (a < b && (cur[a] == ' ' || cur[a] == '\t')) ++a;
        while (b > a && (cur[b - 1] == ' ' || cur[b - 1] == '\t')) --b;
        if (b > a) out.emplace_back(cur.substr(a, b - a));
        cur.clear();
    };
    for (char c : s) {
        if (c == ',') flush();
        else cur += c;
    }
    flush();
    return out;
}

std::string JoinCSV(const std::vector<std::string>& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += ", ";
        out += v[i];
    }
    return out;
}

// ---- Keymap キーキャプチャ用 --------------------------------------------------

// VK → 設定で使うキー名（"Tab" / "Kanji" / ...）。tsf 側 VkToKeyName を共有しない
// （トレイ EXE は yoshinani.dll に依存しない設計）ため、ここでミラーを持つ。
// 対応表を変えるときは src/tsf/KeyMap.cpp の VkToKeyName / KeyNameToVk と同期させる。
std::optional<std::string> VkToTrayName(WPARAM vk) {
    switch (vk) {
        case VK_TAB:         return std::string("Tab");
        case VK_SPACE:       return std::string("Space");
        case VK_OEM_PERIOD:  return std::string("Period");
        case VK_OEM_COMMA:   return std::string("Comma");
        case VK_RETURN:      return std::string("Enter");
        case VK_KANJI:
        case VK_OEM_AUTO:
        case VK_OEM_ENLW:    return std::string("Kanji");
        case VK_NONCONVERT:  return std::string("NonConvert");
        case VK_CONVERT:     return std::string("Convert");
        case VK_CAPITAL:     return std::string("Capital");
        default:             return std::nullopt;
    }
}

// ---- モデル候補 --------------------------------------------------------------

struct ModelChoice {
    const wchar_t* label;
    const char*    value;  // 空 = バックエンド既定
};

const std::vector<ModelChoice> kOpenAiModels = {
    {L"(バックエンド既定: gpt-5.4-mini)", ""},
    {L"gpt-5.4-nano",                     "gpt-5.4-nano"},
    {L"gpt-5.4-mini (採用)",              "gpt-5.4-mini"},
    {L"gpt-5.4 (フル)",                   "gpt-5.4"},
};
const std::vector<ModelChoice> kOllamaModels = {
    {L"(バックエンド既定: gemma4:e4b-it-qat)", ""},
    {L"gemma4:e2b-it-qat",                     "gemma4:e2b-it-qat"},
    {L"gemma4:e4b-it-qat (採用)",              "gemma4:e4b-it-qat"},
};
const std::vector<ModelChoice> kEfforts = {
    {L"none",          "none"},
    {L"low (採用)",    "low"},
    {L"medium",        "medium"},
    {L"high",          "high"},
    {L"xhigh",         "xhigh"},
};

const std::vector<ModelChoice>& ModelsFor(const std::string& backend) {
    return (backend == "ollama") ? kOllamaModels : kOpenAiModels;
}

// ---- レイアウト・コントロール ID ---------------------------------------------

// クライアント領域の論理寸法。DPI Aware マニフェスト有効時は OS がスケールするので、
// 余裕を持って大きめに取る（小さいと下段ボタンが見切れる）。
constexpr int W_MAIN = 620;
constexpr int H_MAIN = 460;

// タブ・ボタン
constexpr int ID_TAB     = 100;
constexpr int ID_OK      = 101;
constexpr int ID_CANCEL  = 102;
constexpr int ID_APPLY   = 103;

// Keymap タブ — 4 項目 × [EDIT + 記録ボタン + クリアボタン]
//   ID_KM_BASE + i*3 + 0/1/2 = edit / btnRecord / btnClear（i=0..3）
constexpr int ID_KM_BASE    = 200;
constexpr int ID_KM_PER_ROW = 3;
constexpr int KM_ROW_COUNT  = 4;
constexpr int KM_TRIGGER    = 0;  // 確定トリガー（triggerKeys）
constexpr int KM_TOGGLE     = 1;  // モード切替トグル（modeToggleKeys）
constexpr int KM_CONV_ON    = 2;  // 直接→変換 へ（conversionOnKeys）
constexpr int KM_DIRECT_ON  = 3;  // 変換→直接 へ（directOnKeys）

// General タブ
constexpr int ID_GN_PATH        = 300;
constexpr int ID_GN_OPEN_JSON   = 301;
constexpr int ID_GN_OPEN_FOLDER = 302;

// Model タブ
constexpr int ID_MD_BACKEND_OPENAI = 400;
constexpr int ID_MD_BACKEND_OLLAMA = 401;
constexpr int ID_MD_MODEL          = 402;
constexpr int ID_MD_EFFORT         = 403;
constexpr int ID_MD_EFFORT_LABEL   = 404;

// タブインデックス（General を最左に・初期表示）
constexpr int TAB_GENERAL = 0;
constexpr int TAB_KEYMAP  = 1;
constexpr int TAB_MODEL   = 2;

// ---- ウィンドウ状態 ----------------------------------------------------------

struct State {
    HINSTANCE hInst = nullptr;
    HFONT     hFont = nullptr;

    HWND hWnd  = nullptr;
    HWND hTab  = nullptr;
    HWND hOK   = nullptr;
    HWND hCancel = nullptr;
    HWND hApply  = nullptr;

    // Keymap — 4 行構成（ラベル + EDIT + 記録 + クリア）
    HWND hKmLabel[KM_ROW_COUNT]  = {};
    HWND hKmEdit [KM_ROW_COUNT]  = {};
    HWND hKmRec  [KM_ROW_COUNT]  = {};
    HWND hKmClr  [KM_ROW_COUNT]  = {};
    HWND hKmHelp = nullptr;
    bool kmRecording[KM_ROW_COUNT] = {};  // 記録モードフラグ（true 中は次の KeyDown を吸い込む）

    // General
    HWND hGnPathLbl    = nullptr;
    HWND hGnPath       = nullptr;
    HWND hGnOpenJson   = nullptr;
    HWND hGnOpenFolder = nullptr;
    HWND hGnHelp       = nullptr;

    // Model
    HWND hMdBackendLbl = nullptr;
    HWND hMdBackendO   = nullptr;  // OpenAI
    HWND hMdBackendL   = nullptr;  // Ollama
    HWND hMdModelLbl   = nullptr;
    HWND hMdModel      = nullptr;
    HWND hMdEffortLbl  = nullptr;
    HWND hMdEffort     = nullptr;
    HWND hMdHelp       = nullptr;

    Settings working;  // 編集中の値（Apply で書き戻し、Cancel で破棄）
};

State* g_state = nullptr;  // モーダルレスだが同時に1枚のみ → グローバルで十分

// ---- ヘルパ ------------------------------------------------------------------

void SetText(HWND h, const std::wstring& s) { SetWindowTextW(h, s.c_str()); }
void SetText(HWND h, const std::string& s)  { SetText(h, Widen(s)); }

std::wstring GetTextW(HWND h) {
    const int n = GetWindowTextLengthW(h);
    if (n <= 0) return {};
    // GetWindowTextW は末尾 NUL を含めて (n+1) 文字書く。
    // wstring(n, ...) で確保すると buf[n] が領域外（UB）になるので n+1 で取り、
    // 戻ってきてから n に縮める。
    std::wstring w(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(h, w.data(), n + 1);
    w.resize(static_cast<size_t>(n));
    return w;
}

void ComboFill(HWND h, const std::vector<ModelChoice>& items, const std::string& selectedValue) {
    SendMessageW(h, CB_RESETCONTENT, 0, 0);
    int sel = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(items[i].label));
        if (selectedValue == items[i].value) sel = static_cast<int>(i);
    }
    SendMessageW(h, CB_SETCURSEL, sel, 0);
}

std::string ComboPickValue(HWND h, const std::vector<ModelChoice>& items) {
    const LRESULT idx = SendMessageW(h, CB_GETCURSEL, 0, 0);
    if (idx < 0 || static_cast<size_t>(idx) >= items.size()) return {};
    return items[static_cast<size_t>(idx)].value;
}

void ShowMany(HWND* arr, size_t n, bool show) {
    for (size_t i = 0; i < n; ++i) ShowWindow(arr[i], show ? SW_SHOW : SW_HIDE);
}

// 前方宣言（KmEditProc から後段のヘルパを呼ぶ）。
std::vector<std::string>* KmTargetVec(int row);
void                      RefreshKmRow(int row);

// ---- Keymap EDIT サブクラス: 記録中の WM_KEYDOWN を捕まえる ------------------

// 各行の「記録」ボタン押下中、EDIT にフォーカスを当てて次の KeyDown を捕獲する。
// uIdSubclass は行インデックス (0..KM_ROW_COUNT-1)。
LRESULT CALLBACK KmEditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                            UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/) {
    if (!g_state) return DefSubclassProc(hWnd, msg, wParam, lParam);
    const int row = static_cast<int>(uIdSubclass);
    if (row < 0 || row >= KM_ROW_COUNT) return DefSubclassProc(hWnd, msg, wParam, lParam);

    if (g_state->kmRecording[row]) {
        // ダイアログ系のキー（Tab/Enter/方向キー）も全部こちらに取り込む。
        if (msg == WM_GETDLGCODE) return DLGC_WANTALLKEYS;

        if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
            // Esc は記録キャンセル（割当てない）。
            if (wParam == VK_ESCAPE) {
                g_state->kmRecording[row] = false;
                SetText(g_state->hKmRec[row], L"記録");
                RefreshKmRow(row);
                SetFocus(g_state->hKmRec[row]);
                return 0;
            }
            auto name = VkToTrayName(wParam);
            if (name) {
                auto* v = KmTargetVec(row);
                if (v) {
                    v->clear();           // 最小実装は単一キーのみ（複数キー対応は将来）
                    v->push_back(*name);
                    SetText(hWnd, *name);
                }
            }
            g_state->kmRecording[row] = false;
            SetText(g_state->hKmRec[row], L"記録");
            SetFocus(g_state->hKmRec[row]);
            return 0;
        }

        if (msg == WM_KILLFOCUS) {
            // フォーカスが外れたら記録を中断（誤ってクリックしたまま外れる事故対策）。
            g_state->kmRecording[row] = false;
            SetText(g_state->hKmRec[row], L"記録");
        }
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// ---- タブ切替 ----------------------------------------------------------------

void ShowTab(int idx) {
    HWND general[] = { g_state->hGnPathLbl, g_state->hGnPath,
                       g_state->hGnOpenJson, g_state->hGnOpenFolder, g_state->hGnHelp };
    HWND model[] = { g_state->hMdBackendLbl, g_state->hMdBackendO, g_state->hMdBackendL,
                     g_state->hMdModelLbl,   g_state->hMdModel,
                     g_state->hMdEffortLbl,  g_state->hMdEffort, g_state->hMdHelp };
    ShowMany(general, ARRAYSIZE(general), idx == TAB_GENERAL);
    ShowMany(model, ARRAYSIZE(model), idx == TAB_MODEL);

    const bool kmOn = (idx == TAB_KEYMAP);
    for (int i = 0; i < KM_ROW_COUNT; ++i) {
        ShowWindow(g_state->hKmLabel[i], kmOn ? SW_SHOW : SW_HIDE);
        ShowWindow(g_state->hKmEdit [i], kmOn ? SW_SHOW : SW_HIDE);
        ShowWindow(g_state->hKmRec  [i], kmOn ? SW_SHOW : SW_HIDE);
        ShowWindow(g_state->hKmClr  [i], kmOn ? SW_SHOW : SW_HIDE);
    }
    ShowWindow(g_state->hKmHelp, kmOn ? SW_SHOW : SW_HIDE);
}

// ---- Model タブ: バックエンド連動 --------------------------------------------

void ApplyBackendToUI() {
    const bool isOllama = (g_state->working.converter.backend == "ollama");
    SendMessageW(g_state->hMdBackendO, BM_SETCHECK, isOllama ? BST_UNCHECKED : BST_CHECKED, 0);
    SendMessageW(g_state->hMdBackendL, BM_SETCHECK, isOllama ? BST_CHECKED : BST_UNCHECKED, 0);

    ComboFill(g_state->hMdModel, ModelsFor(g_state->working.converter.backend),
              g_state->working.converter.model);

    // reasoningEffort は OpenAI 専用。Ollama 時は Disable しラベルにも (OpenAI のみ) と注記。
    const bool effortEnabled = !isOllama;
    EnableWindow(g_state->hMdEffort, effortEnabled);
    EnableWindow(g_state->hMdEffortLbl, effortEnabled);
    SetText(g_state->hMdEffortLbl,
            std::wstring(L"推論強度 (reasoningEffort):") +
            (effortEnabled ? L"" : L"  (OpenAI のみ)"));

    ComboFill(g_state->hMdEffort, kEfforts, g_state->working.converter.reasoningEffort);
}

// ---- Keymap: 行と Settings フィールドの対応 ----------------------------------

std::vector<std::string>* KmTargetVec(int row) {
    auto& s = g_state->working;
    switch (row) {
        case KM_TRIGGER:   return &s.triggerKeys;
        case KM_TOGGLE:    return &s.modeToggleKeys;
        case KM_CONV_ON:   return &s.conversionOnKeys;
        case KM_DIRECT_ON: return &s.directOnKeys;
        default:           return nullptr;
    }
}

// 行の EDIT に working の先頭キー名を表示（空なら空文字＝未割当）。
void RefreshKmRow(int row) {
    auto* v = KmTargetVec(row);
    if (!v) return;
    SetText(g_state->hKmEdit[row], v->empty() ? std::string{} : v->front());
}

// ---- 書き戻し: フォーム → working ---------------------------------------------

void HarvestForm() {
    auto& s = g_state->working;
    // Keymap タブ: working は記録/クリア操作で逐次更新済み。Apply 時の収集は不要。

    s.converter.backend =
        (SendMessageW(g_state->hMdBackendL, BM_GETCHECK, 0, 0) == BST_CHECKED) ? "ollama"
                                                                                : "openai";
    s.converter.model = ComboPickValue(g_state->hMdModel,
                                       ModelsFor(s.converter.backend));
    s.converter.reasoningEffort = ComboPickValue(g_state->hMdEffort, kEfforts);
    if (s.converter.reasoningEffort.empty()) s.converter.reasoningEffort = "low";
}

// ---- 読込: settings.json → working ・フォーム反映 ----------------------------

void LoadIntoForm() {
    EnsureSettingsDir();  // 表示エラー（「場所が利用できません」）対策で先に作る。
    const auto path = SettingsPath();
    const auto text = path.empty() ? std::string{} : ReadAllText(path);
    g_state->working = text.empty() ? Settings{} : ParseSettings(text);

    for (int i = 0; i < KM_ROW_COUNT; ++i) RefreshKmRow(i);
    SetText(g_state->hGnPath, path);
    ApplyBackendToUI();
}

// ---- 保存 -------------------------------------------------------------------

bool SaveSettings() {
    if (!EnsureSettingsDir()) return false;
    const auto path = SettingsPath();
    if (path.empty()) return false;
    return WriteAtomic(path, SerializeSettings(g_state->working));
}

void OnApply() {
    HarvestForm();
    ApplyBackendToUI();  // バックエンド切替に追従させる
    if (!SaveSettings()) {
        MessageBoxW(g_state->hWnd,
                    L"settings.json の書き込みに失敗しました。\n"
                    L"%APPDATA%\\yoshinani への書き込み権限を確認してください。",
                    L"よしなに 設定", MB_OK | MB_ICONERROR);
    }
}

// ---- ウィンドウプロシージャ --------------------------------------------------

void CreateChildren(HWND hWnd) {
    HINSTANCE hi = g_state->hInst;
    auto* st = g_state;

    // フォント（タホマ風の MS Shell Dlg を使う）。
    st->hFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    // タブ
    // タブはボタン領域（下端 50px 帯）を避けて高さを取る。
    st->hTab = CreateWindowExW(0, WC_TABCONTROLW, L"",
                               WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                               12, 12, W_MAIN - 28, H_MAIN - 80,
                               hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TAB)), hi, nullptr);
    SendMessageW(st->hTab, WM_SETFONT, reinterpret_cast<WPARAM>(st->hFont), TRUE);
    TCITEMW it{};
    it.mask = TCIF_TEXT;
    it.pszText = const_cast<wchar_t*>(L"General");       TabCtrl_InsertItem(st->hTab, TAB_GENERAL, &it);
    it.pszText = const_cast<wchar_t*>(L"Keymap");        TabCtrl_InsertItem(st->hTab, TAB_KEYMAP, &it);
    it.pszText = const_cast<wchar_t*>(L"Model");         TabCtrl_InsertItem(st->hTab, TAB_MODEL, &it);

    // タブ内側矩形を取って、その中にコントロールを配置する。
    RECT rcTab; GetClientRect(st->hTab, &rcTab);
    TabCtrl_AdjustRect(st->hTab, FALSE, &rcTab);
    MapWindowPoints(st->hTab, hWnd, reinterpret_cast<POINT*>(&rcTab), 2);

    const int X  = rcTab.left + 10;
    const int Y0 = rcTab.top  + 10;
    const int CW = rcTab.right - rcTab.left - 20;

    // 共通: 子コントロールは作成時から WS_VISIBLE で見える状態にしておく。
    // タブ内コントロールは ShowTab() の SW_HIDE/SW_SHOW で切替（初期可視でも問題なし）、
    // タブ外コントロール（下段の OK/Cancel/Apply）は常に見えるべきなのでこれが必須。
    constexpr DWORD kVis = WS_CHILD | WS_VISIBLE;
    auto mkLabel = [&](int y, int id, const wchar_t* text) {
        HWND h = CreateWindowExW(0, L"STATIC", text,
                                 kVis | SS_LEFT,
                                 X, y, CW, 18, hWnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 hi, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(st->hFont), TRUE);
        return h;
    };
    auto mkHelp = [&](int y, int h, const wchar_t* text) {
        HWND hw = CreateWindowExW(0, L"STATIC", text,
                                  kVis | SS_LEFT,
                                  X, y, CW, h, hWnd, nullptr, hi, nullptr);
        SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(st->hFont), TRUE);
        return hw;
    };
    auto mkEdit = [&](int y, int id, DWORD extra = 0) {
        HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                 kVis | WS_TABSTOP | ES_AUTOHSCROLL | extra,
                                 X, y, CW, 24, hWnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 hi, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(st->hFont), TRUE);
        return h;
    };
    auto mkButton = [&](int x, int y, int w, int id, const wchar_t* text) {
        HWND h = CreateWindowExW(0, L"BUTTON", text,
                                 kVis | WS_TABSTOP | BS_PUSHBUTTON,
                                 x, y, w, 26, hWnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 hi, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(st->hFont), TRUE);
        return h;
    };
    auto mkRadio = [&](int x, int y, int w, int id, const wchar_t* text, bool first) {
        HWND h = CreateWindowExW(0, L"BUTTON", text,
                                 kVis | WS_TABSTOP | BS_AUTORADIOBUTTON | (first ? WS_GROUP : 0),
                                 x, y, w, 22, hWnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 hi, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(st->hFont), TRUE);
        return h;
    };
    auto mkCombo = [&](int y, int w, int id) {
        HWND h = CreateWindowExW(0, L"COMBOBOX", L"",
                                 kVis | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                 X, y, w, 200, hWnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 hi, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(st->hFont), TRUE);
        return h;
    };

    // ----- Keymap タブ -----（4 行 × [ラベル / EDIT(キー名) / 記録ボタン / クリアボタン]）
    const wchar_t* kmLabels[KM_ROW_COUNT] = {
        L"確定トリガー:",
        L"モード切替（トグル）:",
        L"直接 → 変換 へ:",
        L"変換 → 直接 へ:",
    };
    const int rowH    = 32;
    const int xLabel  = X;
    const int wLabel  = 150;
    const int xEdit   = X + wLabel + 8;
    const int wEdit   = 170;
    const int xRec    = xEdit + wEdit + 8;
    const int wRec    = 90;
    const int xClr    = xRec + wRec + 6;
    const int wClr    = 70;
    for (int i = 0; i < KM_ROW_COUNT; ++i) {
        const int y = Y0 + i * rowH;
        HWND lbl = CreateWindowExW(0, L"STATIC", kmLabels[i], kVis | SS_LEFT,
                                   xLabel, y + 5, wLabel, 18, hWnd, nullptr, hi, nullptr);
        HWND edt = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                   kVis | WS_TABSTOP | ES_AUTOHSCROLL | ES_READONLY,
                                   xEdit, y, wEdit, 24, hWnd,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(
                                       ID_KM_BASE + i * ID_KM_PER_ROW + 0)),
                                   hi, nullptr);
        HWND rec = CreateWindowExW(0, L"BUTTON", L"記録",
                                   kVis | WS_TABSTOP | BS_PUSHBUTTON,
                                   xRec, y, wRec, 26, hWnd,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(
                                       ID_KM_BASE + i * ID_KM_PER_ROW + 1)),
                                   hi, nullptr);
        HWND clr = CreateWindowExW(0, L"BUTTON", L"クリア",
                                   kVis | WS_TABSTOP | BS_PUSHBUTTON,
                                   xClr, y, wClr, 26, hWnd,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(
                                       ID_KM_BASE + i * ID_KM_PER_ROW + 2)),
                                   hi, nullptr);
        SendMessageW(lbl, WM_SETFONT, reinterpret_cast<WPARAM>(st->hFont), TRUE);
        SendMessageW(edt, WM_SETFONT, reinterpret_cast<WPARAM>(st->hFont), TRUE);
        SendMessageW(rec, WM_SETFONT, reinterpret_cast<WPARAM>(st->hFont), TRUE);
        SendMessageW(clr, WM_SETFONT, reinterpret_cast<WPARAM>(st->hFont), TRUE);
        SetWindowSubclass(edt, KmEditProc, static_cast<UINT_PTR>(i), 0);
        st->hKmLabel[i] = lbl; st->hKmEdit[i] = edt; st->hKmRec[i] = rec; st->hKmClr[i] = clr;
    }
    st->hKmHelp = mkHelp(Y0 + KM_ROW_COUNT * rowH + 12, 64,
        L"「記録」ボタンを押してから割当てたいキーを 1 つ押してください。\n"
        L"認識キー: Tab / Period(。) / Comma(、) / Kanji(半角/全角) / NonConvert(無変換) / "
        L"Convert(変換) / Capital(英数) ／ Esc で記録キャンセル。");

    // ----- General タブ -----
    st->hGnPathLbl    = mkLabel(Y0,        0, L"設定ファイル:");
    st->hGnPath       = mkEdit (Y0 + 20, ID_GN_PATH, ES_READONLY);
    st->hGnOpenJson   = mkButton(X,        Y0 + 56, 180, ID_GN_OPEN_JSON,   L"settings.json を開く");
    st->hGnOpenFolder = mkButton(X + 188,  Y0 + 56, 180, ID_GN_OPEN_FOLDER, L"設定フォルダを開く");
    st->hGnHelp       = mkHelp (Y0 + 102, 64,
        L"変更は IME OFF→ON で TIP に反映されます。\n"
        L"%APPDATA%\\yoshinani\\settings.json が無ければ Apply 時に作成します。");

    // ----- Model タブ -----
    st->hMdBackendLbl = mkLabel(Y0,        0, L"バックエンド:");
    st->hMdBackendO   = mkRadio(X,        Y0 + 20, 200, ID_MD_BACKEND_OPENAI,
                                L"OpenAI（クラウド）", true);
    st->hMdBackendL   = mkRadio(X + 210,  Y0 + 20, 200, ID_MD_BACKEND_OLLAMA,
                                L"Ollama（ローカル）", false);

    st->hMdModelLbl   = mkLabel(Y0 + 56,    0, L"モデル:");
    st->hMdModel      = mkCombo(Y0 + 76, CW, ID_MD_MODEL);

    st->hMdEffortLbl  = mkLabel(Y0 + 112,   ID_MD_EFFORT_LABEL, L"推論強度 (reasoningEffort):");
    st->hMdEffort     = mkCombo(Y0 + 132, 200, ID_MD_EFFORT);

    st->hMdHelp       = mkHelp (Y0 + 172, 56,
        L"バックエンドを切替えるとモデルは「バックエンド既定」にリセットされます。\n"
        L"OpenAI モデル既定: gpt-5.4-mini / Ollama モデル既定: gemma4:e4b-it-qat。");

    // ----- 下段ボタン -----（クライアント下端から余裕 50px）
    const int BY = H_MAIN - 50;
    st->hApply  = mkButton(W_MAIN - 100, BY, 80, ID_APPLY,  L"適用");
    st->hCancel = mkButton(W_MAIN - 188, BY, 80, ID_CANCEL, L"キャンセル");
    st->hOK     = mkButton(W_MAIN - 276, BY, 80, ID_OK,     L"OK");

    ShowTab(TAB_GENERAL);  // General を初期表示（パス・操作の入口）
    TabCtrl_SetCurSel(st->hTab, TAB_GENERAL);
}

LRESULT CALLBACK SettingsProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            g_state->hWnd = hWnd;
            CreateChildren(hWnd);
            LoadIntoForm();
            return 0;

        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lParam);
            if (nm->hwndFrom == g_state->hTab && nm->code == TCN_SELCHANGE) {
                ShowTab(TabCtrl_GetCurSel(g_state->hTab));
            }
            return 0;
        }

        case WM_COMMAND: {
            const WORD id  = LOWORD(wParam);
            const WORD code = HIWORD(wParam);

            // Keymap タブの記録/クリアボタン（ID_KM_BASE 帯）。
            if (id >= ID_KM_BASE && id < ID_KM_BASE + KM_ROW_COUNT * ID_KM_PER_ROW) {
                const int offset = id - ID_KM_BASE;
                const int row    = offset / ID_KM_PER_ROW;
                const int kind   = offset % ID_KM_PER_ROW;  // 0=edit / 1=rec / 2=clr
                if (kind == 1 && code == BN_CLICKED) {
                    // 他の行が記録モード中なら先にキャンセル（同時に複数行を記録モードにしない）。
                    for (int j = 0; j < KM_ROW_COUNT; ++j) {
                        if (g_state->kmRecording[j]) {
                            g_state->kmRecording[j] = false;
                            SetText(g_state->hKmRec[j], L"記録");
                            RefreshKmRow(j);
                        }
                    }
                    // 記録モード開始: EDIT に focus、サブクラスが次の KeyDown を捕える。
                    g_state->kmRecording[row] = true;
                    SetText(g_state->hKmRec[row], L"押してください…");
                    SetText(g_state->hKmEdit[row], std::string{});
                    SetFocus(g_state->hKmEdit[row]);
                    return 0;
                }
                if (kind == 2 && code == BN_CLICKED) {
                    if (auto* v = KmTargetVec(row)) v->clear();
                    RefreshKmRow(row);
                    return 0;
                }
                return 0;
            }

            switch (id) {
                case ID_OK:
                    OnApply();
                    DestroyWindow(hWnd);
                    return 0;
                case ID_APPLY:
                    OnApply();
                    return 0;
                case ID_CANCEL:
                    DestroyWindow(hWnd);
                    return 0;

                case ID_MD_BACKEND_OPENAI:
                case ID_MD_BACKEND_OLLAMA:
                    if (code == BN_CLICKED) {
                        // バックエンド切替時はモデルを「バックエンド既定」にリセット。
                        // 残しておくと OpenAI モデル名のまま Ollama に投げて崩れるため。
                        const bool ollama = (id == ID_MD_BACKEND_OLLAMA);
                        g_state->working.converter.backend = ollama ? "ollama" : "openai";
                        g_state->working.converter.model.clear();
                        ApplyBackendToUI();
                    }
                    return 0;

                case ID_GN_OPEN_JSON: {
                    EnsureSettingsDir();
                    const auto path = SettingsPath();
                    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
                        // 未存在なら現在の working を書き出してから開く（編集対象を作る）。
                        SaveSettings();
                    }
                    if (!path.empty()) {
                        ShellExecuteW(hWnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    }
                    return 0;
                }
                case ID_GN_OPEN_FOLDER: {
                    EnsureSettingsDir();
                    const auto dir = SettingsDir();
                    if (!dir.empty()) {
                        ShellExecuteW(hWnd, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    }
                    return 0;
                }
            }
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hWnd);
            return 0;

        case WM_DESTROY:
            // EDIT サブクラスを解除（dangling コールバック回避）。
            for (int i = 0; i < KM_ROW_COUNT; ++i) {
                if (g_state->hKmEdit[i]) {
                    RemoveWindowSubclass(g_state->hKmEdit[i], KmEditProc,
                                         static_cast<UINT_PTR>(i));
                }
            }
            delete g_state;
            g_state = nullptr;
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ウィンドウクラスは1度だけ登録。
const wchar_t kClass[] = L"YoshinaniSettingsWnd";

bool EnsureClass(HINSTANCE hInst) {
    WNDCLASSEXW wc{};
    if (GetClassInfoExW(hInst, kClass, &wc)) return true;
    wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = SettingsProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = kClass;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.hIcon         = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(IDI_TRAY),
                                                     IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    return RegisterClassExW(&wc) != 0;
}

}  // namespace

HWND SettingsWindow::ActiveHwnd() {
    return (g_state && g_state->hWnd && IsWindow(g_state->hWnd)) ? g_state->hWnd : nullptr;
}

void SettingsWindow::Show(HINSTANCE hInst) {
    // 既に開いていれば前面化して終わり。
    if (g_state && g_state->hWnd && IsWindow(g_state->hWnd)) {
        if (IsIconic(g_state->hWnd)) ShowWindow(g_state->hWnd, SW_RESTORE);
        SetForegroundWindow(g_state->hWnd);
        return;
    }

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_TAB_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    if (!EnsureClass(hInst)) return;

    g_state = new State();
    g_state->hInst = hInst;

    // 画面中央に配置（DPI を厳密に拾わない簡易計算）。
    const int sw = GetSystemMetrics(SM_CXSCREEN);
    const int sh = GetSystemMetrics(SM_CYSCREEN);

    // W_MAIN x H_MAIN は「クライアント領域」として確保したい寸法。
    // CreateWindowExW に渡す w/h は外形寸法なので AdjustWindowRect で枠/タイトルバー分を加算する
    // （これを忘れると下段の OK/キャンセル/適用がタイトルバー分だけ下に押し出されて見切れる）。
    constexpr DWORD kStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    constexpr DWORD kExStyle = WS_EX_DLGMODALFRAME;
    RECT rcOuter{ 0, 0, W_MAIN, H_MAIN };
    AdjustWindowRectEx(&rcOuter, kStyle, FALSE, kExStyle);
    const int outerW = rcOuter.right  - rcOuter.left;
    const int outerH = rcOuter.bottom - rcOuter.top;
    HWND h = CreateWindowExW(kExStyle, kClass, L"よしなに 設定", kStyle,
                             (sw - outerW) / 2, (sh - outerH) / 2, outerW, outerH,
                             nullptr, nullptr, hInst, nullptr);
    if (!h) {
        delete g_state;
        g_state = nullptr;
        return;
    }
    ShowWindow(h, SW_SHOW);
    UpdateWindow(h);
    SetForegroundWindow(h);
}

}  // namespace yoshinani::tray
