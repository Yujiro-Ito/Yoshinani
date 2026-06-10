# yoshinani.tray — 通知領域アイコン（タスクトレイ）の常駐 EXE。
# TIP DLL は全アプリにインプロセスロードされるため、UI を載せると全アプリが太る。
# → 設定切替 UI は別プロセスに分離する（MAIN_SPEC §3 と同じ「重い物は別プロセス」哲学）。
# 設定書き戻し先: %APPDATA%\yoshinani\settings.json（TIP は次の Activate で読み直す）。
add_executable(yoshinani.tray WIN32
  main.cpp
  SettingsWindow.cpp
  yoshinani-tray.rc   # IDI_TRAY ← assets/yoshinani.ico
)
set_target_properties(yoshinani.tray PROPERTIES OUTPUT_NAME "yoshinani-tray")
target_compile_definitions(yoshinani.tray PRIVATE UNICODE _UNICODE)
target_link_libraries(yoshinani.tray PRIVATE
  yoshinani.core   # ParseSettings / SerializeSettings（書式は TIP と同一）
  shell32          # Shell_NotifyIcon / SHGetKnownFolderPath / ShellExecute
  comctl32         # SysTabControl32（タブ）・InitCommonControlsEx
  user32           # メニュー・ウィンドウ
)

# マニフェストは .rc の RT_MANIFEST 経由で埋める（comctl32 v6 / Visual Styles / DPI Aware）。
# CMake/vs_link_exe ラッパーは既定でリンカ自動マニフェスト（/MANIFEST + manifest.res）を
# 生成し .rc の `1 RT_MANIFEST` と衝突（CVT1100）するため、自動側を /MANIFEST:NO で抑制。
target_link_options(yoshinani.tray PRIVATE "/MANIFEST:NO")
