# yoshinani.tray — 通知領域アイコン（タスクトレイ）の常駐 EXE。
# TIP DLL は全アプリにインプロセスロードされるため、UI を載せると全アプリが太る。
# → 設定切替 UI は別プロセスに分離する（MAIN_SPEC §3 と同じ「重い物は別プロセス」哲学）。
# 設定書き戻し先: %APPDATA%\yoshinani\settings.json（TIP は次の Activate で読み直す）。
add_executable(yoshinani.tray WIN32
  main.cpp
)
set_target_properties(yoshinani.tray PROPERTIES OUTPUT_NAME "yoshinani-tray")
target_compile_definitions(yoshinani.tray PRIVATE UNICODE _UNICODE)
target_link_libraries(yoshinani.tray PRIVATE
  yoshinani.core   # ParseSettings / SerializeSettings（書式は TIP と同一）
  shell32          # Shell_NotifyIcon / SHGetKnownFolderPath / ShellExecute
  comctl32         # 将来の UI 拡張用（必須ではないが Win32 GUI の定番）
  user32           # メニュー・ウィンドウ
)
