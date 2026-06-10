// 4-A 非同期変換のスレッド橋渡し — 実装。
#include "ConvertMarshaller.h"

#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace yoshinani::tsf {

using yoshinani::core::domain::ConversionResult;
using yoshinani::core::domain::RequestId;

namespace {
constexpr UINT WM_YOSHINANI_RESULT = WM_APP + 1;
constexpr wchar_t kWndClass[] = L"YoshinaniConvertMarshaller";
}  // namespace

struct ConvertMarshaller::State {
    std::mutex mutex;
    HWND hwnd = nullptr;                                  // null = Stop 済み（mutex 保護下で読む）
    std::unordered_map<RequestId, ConversionResult> results;  // 到着済み・未取り出しの結果
    ResultHandler onResult;                               // TIP スレッドでのみ呼ぶ
};

// ウィンドウプロシージャ: ワーカーが Post した結果を TIP スレッドで取り出して通知する。
static LRESULT CALLBACK MarshallerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_YOSHINANI_RESULT) {
        auto* state = reinterpret_cast<ConvertMarshaller::State*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (state) {
            const RequestId id = static_cast<RequestId>(wParam);
            ConversionResult result;
            bool found = false;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                auto it = state->results.find(id);
                if (it != state->results.end()) {
                    result = std::move(it->second);
                    state->results.erase(it);
                    found = true;
                }
            }
            // ハンドラはロック外で呼ぶ（中で edit session を要求するため）。
            if (found && state->onResult) state->onResult(id, std::move(result));
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ConvertMarshaller::Start(ResultHandler onResult) {
    Stop();
    auto state = std::make_shared<State>();
    state->onResult = std::move(onResult);

    WNDCLASSW wc{};
    wc.lpfnWndProc   = MarshallerWndProc;
    wc.hInstance     = g_hInst;
    wc.lpszClassName = kWndClass;
    RegisterClassW(&wc);  // 二重登録は失敗するが既存クラスが使えるので無視してよい

    // メッセージ専用ウィンドウ（HWND_MESSAGE）: 表示されず、TIP スレッドの
    // メッセージポンプ（アプリが回す）で WndProc が呼ばれる。
    HWND hwnd = CreateWindowExW(0, kWndClass, L"", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, g_hInst, nullptr);
    if (!hwnd) return false;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state.get()));
    state->hwnd = hwnd;
    state_ = std::move(state);
    return true;
}

void ConvertMarshaller::Stop() {
    if (!state_) return;
    HWND hwnd = nullptr;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        hwnd = state_->hwnd;
        state_->hwnd = nullptr;      // 以後ワーカーは Post しない（結果は捨てられる）
        state_->onResult = nullptr;  // State の書き換えは必ず mutex 内で（一貫性）
    }
    if (hwnd) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        DestroyWindow(hwnd);
    }
    state_.reset();
}

void ConvertMarshaller::Dispatch(
        std::shared_ptr<yoshinani::core::domain::IKanaKanjiConverter> converter,
        RequestId id, std::u16string input) {
    if (!state_ || !converter) return;
    std::shared_ptr<State> state = state_;  // ワーカーと共有（Stop 後も安全に書ける）

    // detach スレッドは DllCanUnloadNow の参照カウントに乗らない。実行中に DLL が
    // アンロードされるとクラッシュするため、スレッド生存中は DllAddRef で保持する。
    DllAddRef();
    try {
        std::thread([converter = std::move(converter), id, input = std::move(input), state]() {
            // 同期実装の Convert をワーカーで実行（callback は同スレッドで即時に来る）。
            converter->Convert(id, input, [&state](RequestId rid, ConversionResult r) {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (!state->hwnd) return;  // Stop 済み → 捨てる
                state->results[rid] = std::move(r);
                PostMessageW(state->hwnd, WM_YOSHINANI_RESULT, static_cast<WPARAM>(rid), 0);
            });
            DllRelease();
        }).detach();
    } catch (...) {
        // スレッド生成失敗（極稀）。結果は届かずセグメントは先頭で滞留するが、
        // Esc（全取消）/ Enter（全生確定）でユーザーが回収できる。
        DllRelease();
    }
}

}  // namespace yoshinani::tsf
