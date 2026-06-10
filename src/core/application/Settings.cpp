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
        if (j.contains("modeToggleKeys") && j.at("modeToggleKeys").is_array()) {
            std::vector<std::string> keys;
            for (const auto& e : j.at("modeToggleKeys")) {
                if (e.is_string()) keys.push_back(e.get<std::string>());
            }
            if (!keys.empty()) s.modeToggleKeys = std::move(keys);
        }
        // conversionOnKeys / directOnKeys は空配列でも「指定された」と尊重する
        // （Google 日本語入力流の片方向遷移を有効化したいときは明示的に空でない配列を入れる）。
        if (j.contains("conversionOnKeys") && j.at("conversionOnKeys").is_array()) {
            s.conversionOnKeys.clear();
            for (const auto& e : j.at("conversionOnKeys")) {
                if (e.is_string()) s.conversionOnKeys.push_back(e.get<std::string>());
            }
        }
        if (j.contains("directOnKeys") && j.at("directOnKeys").is_array()) {
            s.directOnKeys.clear();
            for (const auto& e : j.at("directOnKeys")) {
                if (e.is_string()) s.directOnKeys.push_back(e.get<std::string>());
            }
        }
        if (j.contains("converter") && j.at("converter").is_object()) {
            const auto& c = j.at("converter");
            // 空文字は「指定なし」とみなし既定を保つ（タスクトレイ UI が部分的に書く場合に安全）。
            if (c.contains("backend") && c.at("backend").is_string() &&
                !c.at("backend").get<std::string>().empty()) {
                s.converter.backend = c.at("backend").get<std::string>();
            }
            if (c.contains("model") && c.at("model").is_string() &&
                !c.at("model").get<std::string>().empty()) {
                s.converter.model = c.at("model").get<std::string>();
            }
            if (c.contains("reasoningEffort") && c.at("reasoningEffort").is_string() &&
                !c.at("reasoningEffort").get<std::string>().empty()) {
                s.converter.reasoningEffort = c.at("reasoningEffort").get<std::string>();
            }
        }
    } catch (...) {
        // 不正JSONは握りつぶして既定にフォールバック（IME を落とさない）。
    }
    return s;
}

std::string SerializeSettings(const Settings& s) {
    nlohmann::json j;
    j["triggerKeys"]      = s.triggerKeys;
    j["modeToggleKeys"]   = s.modeToggleKeys;
    j["conversionOnKeys"] = s.conversionOnKeys;
    j["directOnKeys"]     = s.directOnKeys;
    j["converter"] = {
        {"backend",         s.converter.backend},
        {"model",           s.converter.model},
        {"reasoningEffort", s.converter.reasoningEffort},
    };
    return j.dump(2);
}

}  // namespace yoshinani::core::application
