// 4-A 非同期変換のスレッド橋渡し（infra）。
// 変換（WinHTTP 同期）はワーカースレッドで実行し、結果はメッセージ専用ウィンドウ
// 経由で TIP スレッドへ戻す。TSF の edit session は TIP のスレッドからしか要求
// できないため、このマーシャリングが必須。
//
// ライフタイム: ワーカーは detach するため、共有状態は shared_ptr で持ち、
// Stop() 後に届いた結果は黙って捨てる（ウィンドウ破棄済み → Post しない）。
#pragma once
#include "Globals.h"

#include <functional>
#include <memory>
#include <string>

#include "domain/ports/IKanaKanjiConverter.h"

namespace yoshinani::tsf {

class ConvertMarshaller {
public:
    using ResultHandler =
        std::function<void(yoshinani::core::domain::RequestId,
                           yoshinani::core::domain::ConversionResult)>;

    ConvertMarshaller() = default;
    ~ConvertMarshaller() { Stop(); }
    ConvertMarshaller(const ConvertMarshaller&) = delete;
    ConvertMarshaller& operator=(const ConvertMarshaller&) = delete;

    // TIP スレッドで呼ぶ。メッセージ専用ウィンドウを作り、結果到着時に
    // onResult を「TIP スレッドで」呼び出すようにする。
    bool Start(ResultHandler onResult);

    // ウィンドウを破棄し、以後の結果を無視する（Deactivate 時）。TIP スレッドで呼ぶ。
    void Stop();

    // ワーカースレッドで converter->Convert を実行する（投げて戻る）。
    //   shared_ptr で受けるのは、Deactivate（converter 解放）後もワーカーが
    //   実行中でありうるため（共有保持で use-after-free を防ぐ）。
    //   Convert 実装はステートレス（呼び出しごとに独立した接続）なので並行呼び出し可。
    void Dispatch(std::shared_ptr<yoshinani::core::domain::IKanaKanjiConverter> converter,
                  yoshinani::core::domain::RequestId id,
                  yoshinani::core::domain::ConversionInput input);

    // 共有状態（結果バッファ + ウィンドウハンドル）。実体は .cpp（WndProc から触るため public 宣言）。
    struct State;

private:
    std::shared_ptr<State> state_;
};

}  // namespace yoshinani::tsf
