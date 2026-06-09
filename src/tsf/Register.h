// 1-A TSF スケルトン — COM 自己登録 / TSF プロファイル・カテゴリ登録。
#pragma once

// COM サーバ登録（HKCR\CLSID\{CLSID}\InProcServer32）
BOOL RegisterServer();
void UnregisterServer();

// TSF 言語プロファイル登録（ITfInputProcessorProfiles, ja-JP）
BOOL RegisterProfiles();
void UnregisterProfiles();

// TSF カテゴリ登録（GUID_TFCAT_TIP_KEYBOARD）
BOOL RegisterCategories();
void UnregisterCategories();
