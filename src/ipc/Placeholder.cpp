// yoshinani.ipc プレースホルダ（3-C で PipeKanaKanjiConverter を実装）。
// 空の翻訳単位だと MSVC が LNK4221 を出すため、ダミーシンボルを 1 つ置く。
namespace yoshinani::ipc {
int reserved() noexcept { return 0; }
}  // namespace yoshinani::ipc
