// yoshinani.tsf スタブ — DllMain のみ。
// COM サーバ実装（DllGetClassObject / DllCanUnloadNow / DllRegisterServer /
// DllUnregisterServer / クラスファクトリ）と TSF 登録は 1-A で実装する。
#include <windows.h>

BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD reason, LPVOID /*lpReserved*/) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
