// 1-A TSF スケルトン — 共通宣言（GUID・識別子・DLL 状態）
#pragma once

#include <windows.h>
#include <msctf.h>

// CLSID / 言語プロファイル GUID（実体は Globals.cpp で INITGUID 定義）
EXTERN_C const CLSID CLSID_Yoshinani;
EXTERN_C const GUID  GUID_YoshinaniProfile;

// 識別子（MAIN_SPEC「名称・識別子（統一）」準拠）
//   ソースは /utf-8 でコンパイルするため L"よしなに" は正しく UTF-16 になる。
#define YOSHINANI_DESC    L"よしなに"
#define YOSHINANI_LANGID  MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN) // ja-JP = 0x0411

// DLL 全体の状態
extern HINSTANCE g_hInst;
extern LONG      g_cRefDll;   // DLL 参照カウント（DllCanUnloadNow 判定）

inline void DllAddRef()  { InterlockedIncrement(&g_cRefDll); }
inline void DllRelease() { InterlockedDecrement(&g_cRefDll); }
