// 設定 JSON パーサ（nlohmann/json。OS非依存 → core でユニットテスト可能）。
#include "application/Settings.h"

#include <nlohmann/json.hpp>

namespace yoshinani::core::application {

Settings ParseSettings(const std::string& jsonText) {
    Settings s;  // 既定（triggerKeys = ["Tab"]）
    try {
        const auto j = nlohmann::json::parse(jsonText);
        if (j.contains("triggerKeys") && j.at("triggerKeys").is_array()) {
            std::vector<std::string> keys;
            for (const auto& e : j.at("triggerKeys")) {
                if (e.is_string()) keys.push_back(e.get<std::string>());
            }
            if (!keys.empty()) s.triggerKeys = std::move(keys);
        }
    } catch (...) {
        // 不正JSONは握りつぶして既定にフォールバック（IME を落とさない）。
    }
    return s;
}

}  // namespace yoshinani::core::application
