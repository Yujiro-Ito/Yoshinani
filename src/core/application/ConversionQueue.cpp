// 3-D 変換キュー — 実装。
#include "application/ConversionQueue.h"

#include <utility>

namespace yoshinani::core::application {

bool ConversionQueue::TryEnqueue(ConversionRequest req) {
    if (Full()) return false;
    items_.push_back(std::move(req));
    return true;
}

bool ConversionQueue::MarkDone(RequestId id, std::u16string result) {
    for (auto& r : items_) {
        if (r.id == id && r.state == ConvState::Pending) {  // 一方向: Pending のときだけ遷移
            r.state = ConvState::Done;
            r.result = std::move(result);
            return true;
        }
    }
    return false;
}

bool ConversionQueue::MarkFailed(RequestId id) {
    for (auto& r : items_) {
        if (r.id == id && r.state == ConvState::Pending) {  // 一方向: Pending のときだけ遷移
            r.state = ConvState::Failed;
            return true;
        }
    }
    return false;
}

std::optional<ConversionRequest> ConversionQueue::PopReadyInOrder() {
    if (items_.empty()) return std::nullopt;
    if (items_.front().state == ConvState::Pending) return std::nullopt;
    ConversionRequest r = std::move(items_.front());
    items_.pop_front();
    return r;
}

}  // namespace yoshinani::core::application
