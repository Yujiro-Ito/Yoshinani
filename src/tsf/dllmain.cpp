// 1-A TSF スケルトン — DLL エントリと COM 標準エクスポート。
#include "Globals.h"
#include "ClassFactory.h"
#include "Register.h"
#include <olectl.h>   // DllRegisterServer / DllUnregisterServer のプロトタイプ
#include <new>

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*lpReserved*/) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_hInst = static_cast<HINSTANCE>(hModule);
            DisableThreadLibraryCalls(hModule);
            break;
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (ppv) *ppv = nullptr;
    if (IsEqualCLSID(rclsid, CLSID_Yoshinani)) {
        CClassFactory* pFactory = new (std::nothrow) CClassFactory();
        if (!pFactory) return E_OUTOFMEMORY;
        HRESULT hr = pFactory->QueryInterface(riid, ppv);
        pFactory->Release();
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

STDAPI DllCanUnloadNow() {
    return (g_cRefDll <= 0) ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer() {
    if (!RegisterServer())     { DllUnregisterServer(); return E_FAIL; }
    if (!RegisterProfiles())   { DllUnregisterServer(); return E_FAIL; }
    if (!RegisterCategories()) { DllUnregisterServer(); return E_FAIL; }
    return S_OK;
}

STDAPI DllUnregisterServer() {
    UnregisterCategories();
    UnregisterProfiles();
    UnregisterServer();
    return S_OK;
}
