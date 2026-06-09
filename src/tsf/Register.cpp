// 1-A TSF スケルトン — 登録/解除の実装。
#include "Globals.h"
#include "Register.h"
#include <strsafe.h>

static const WCHAR c_szInProcSvr32[] = L"InProcServer32";
static const WCHAR c_szModelName[]   = L"Apartment";

// GUID を "{........-....-....-....-............}" 形式の文字列に。
static BOOL CLSIDToString(REFGUID guid, WCHAR* out, size_t cch) {
    LPOLESTR psz = nullptr;
    if (FAILED(StringFromCLSID(guid, &psz))) return FALSE;
    HRESULT hr = StringCchCopyW(out, cch, psz);
    CoTaskMemFree(psz);
    return SUCCEEDED(hr);
}

static DWORD ByteLen(const WCHAR* s) {
    return static_cast<DWORD>((lstrlenW(s) + 1) * sizeof(WCHAR));
}

BOOL RegisterServer() {
    WCHAR clsid[64];
    if (!CLSIDToString(CLSID_Yoshinani, clsid, ARRAYSIZE(clsid))) return FALSE;

    WCHAR keyPath[128];
    if (FAILED(StringCchPrintfW(keyPath, ARRAYSIZE(keyPath), L"CLSID\\%s", clsid))) return FALSE;

    HKEY hKey = nullptr, hSub = nullptr;
    BOOL ok = FALSE;

    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath, 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, nullptr, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(YOSHINANI_DESC), ByteLen(YOSHINANI_DESC));

        if (RegCreateKeyExW(hKey, c_szInProcSvr32, 0, nullptr, REG_OPTION_NON_VOLATILE,
                            KEY_WRITE, nullptr, &hSub, nullptr) == ERROR_SUCCESS) {
            WCHAR dllPath[MAX_PATH];
            DWORD n = GetModuleFileNameW(g_hInst, dllPath, ARRAYSIZE(dllPath));
            if (n > 0 && n < ARRAYSIZE(dllPath)) {
                RegSetValueExW(hSub, nullptr, 0, REG_SZ,
                               reinterpret_cast<const BYTE*>(dllPath), ByteLen(dllPath));
                RegSetValueExW(hSub, L"ThreadingModel", 0, REG_SZ,
                               reinterpret_cast<const BYTE*>(c_szModelName), ByteLen(c_szModelName));
                ok = TRUE;
            }
            RegCloseKey(hSub);
        }
        RegCloseKey(hKey);
    }
    return ok;
}

void UnregisterServer() {
    WCHAR clsid[64];
    if (!CLSIDToString(CLSID_Yoshinani, clsid, ARRAYSIZE(clsid))) return;

    WCHAR keyPath[128];
    if (FAILED(StringCchPrintfW(keyPath, ARRAYSIZE(keyPath), L"CLSID\\%s", clsid))) return;

    RegDeleteTreeW(HKEY_CLASSES_ROOT, keyPath);
}

BOOL RegisterProfiles() {
    ITfInputProcessorProfiles* pProfiles = nullptr;
    if (FAILED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                IID_ITfInputProcessorProfiles,
                                reinterpret_cast<void**>(&pProfiles)))) {
        return FALSE;
    }

    BOOL ok = FALSE;
    if (SUCCEEDED(pProfiles->Register(CLSID_Yoshinani))) {
        HRESULT hr = pProfiles->AddLanguageProfile(
            CLSID_Yoshinani, YOSHINANI_LANGID, GUID_YoshinaniProfile,
            YOSHINANI_DESC, static_cast<ULONG>(lstrlenW(YOSHINANI_DESC)),
            nullptr, 0, 0);
        ok = SUCCEEDED(hr);
    }
    pProfiles->Release();
    return ok;
}

void UnregisterProfiles() {
    ITfInputProcessorProfiles* pProfiles = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfInputProcessorProfiles,
                                   reinterpret_cast<void**>(&pProfiles)))) {
        pProfiles->Unregister(CLSID_Yoshinani);
        pProfiles->Release();
    }
}

// 登録するカテゴリ一覧。
//   - GUID_TFCAT_TIP_KEYBOARD          : キーボード TIP（必須）
//   - GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT: イマーシブ／パッケージ系アプリ
//       （Win11 の新メモ帳・ストアアプリ等）で入力エンジンとして選ばれるための宣言。
//       これが無いと従来型 Win32 アプリでしか有効化されない（メモ帳で素通りする原因）。
//   - GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT : システムトレイ（言語切替 UI）対応の宣言。
static const GUID* const c_supportedCategories[] = {
    &GUID_TFCAT_TIP_KEYBOARD,
    &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
    &GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
};

BOOL RegisterCategories() {
    ITfCategoryMgr* pCat = nullptr;
    if (FAILED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                IID_ITfCategoryMgr, reinterpret_cast<void**>(&pCat)))) {
        return FALSE;
    }
    HRESULT hr = S_OK;
    for (const GUID* cat : c_supportedCategories) {
        HRESULT h = pCat->RegisterCategory(CLSID_Yoshinani, *cat, CLSID_Yoshinani);
        if (FAILED(h)) hr = h;  // いずれか失敗したら失敗扱い（最後のエラーを保持）
    }
    pCat->Release();
    return SUCCEEDED(hr);
}

void UnregisterCategories() {
    ITfCategoryMgr* pCat = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfCategoryMgr, reinterpret_cast<void**>(&pCat)))) {
        for (const GUID* cat : c_supportedCategories) {
            pCat->UnregisterCategory(CLSID_Yoshinani, *cat, CLSID_Yoshinani);
        }
        pCat->Release();
    }
}
