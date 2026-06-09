# yoshinani.tsf — infra（→ yoshinani.dll。TSF/COM。Windows のみ）
# 1-A: 最小 TIP（COM サーバ + TSF プロファイル/カテゴリ登録）。
# キー捕捉・preedit（1-B）/ トリガー確定（1-C）は順次追加する。
add_library(yoshinani.tsf SHARED
  dllmain.cpp
  Globals.cpp
  ClassFactory.cpp
  TextService.cpp
  Register.cpp
  KeyMap.cpp
  yoshinani.def        # .def を source に入れると CMake が /DEF として渡す（COM エクスポート）
)
set_target_properties(yoshinani.tsf PROPERTIES OUTPUT_NAME "yoshinani")
target_compile_definitions(yoshinani.tsf PRIVATE UNICODE _UNICODE)
target_link_libraries(yoshinani.tsf PRIVATE
  yoshinani.core
  yoshinani.ipc
  ole32      # CoCreateInstance / StringFromCLSID / CoTaskMemFree
  advapi32   # Reg* / RegDeleteTreeW
)
