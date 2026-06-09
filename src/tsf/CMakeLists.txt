# yoshinani.tsf — infra（→ yoshinani.dll。TSF/COM。Windows のみ）
# 現状は DllMain だけのスタブ DLL。
# COM エクスポート（DllGetClassObject / DllRegisterServer 等）と .def は 1-A で追加する。
add_library(yoshinani.tsf SHARED
  dllmain.cpp
)
set_target_properties(yoshinani.tsf PROPERTIES OUTPUT_NAME "yoshinani")
target_compile_definitions(yoshinani.tsf PRIVATE UNICODE _UNICODE)
target_link_libraries(yoshinani.tsf PRIVATE yoshinani.core yoshinani.ipc)
