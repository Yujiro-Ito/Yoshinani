// 1-A TSF スケルトン — GUID 実体と DLL 状態の定義。
// INITGUID を有効にしてから各ヘッダを取り込むことで、msctf.h 等が宣言する
// TSF の CLSID/IID（CLSID_TF_InputProcessorProfiles 等）の実体を本 TU に確保する。
// ※ INITGUID を使う TU はプロジェクト内でここ1つだけ（重複定義回避）。
#include <initguid.h>
#include "Globals.h"

// 自前 GUID の実体（PowerShell New-Guid で生成・固定）
DEFINE_GUID(CLSID_Yoshinani,
    0x2405C199, 0x9A3D, 0x47FC, 0xA6, 0x62, 0x18, 0x43, 0x78, 0x97, 0x3C, 0x12);
DEFINE_GUID(GUID_YoshinaniProfile,
    0xD73A3464, 0xBA72, 0x4ED3, 0xB0, 0x11, 0xAE, 0x4D, 0xC0, 0x2B, 0xED, 0x7B);

HINSTANCE g_hInst   = nullptr;
LONG      g_cRefDll = 0;
