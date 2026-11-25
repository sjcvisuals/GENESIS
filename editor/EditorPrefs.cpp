#include "editor/EditorPrefs.hpp"

#include "platform/EditorPaths.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <fstream>

namespace genesis {

namespace {

constexpr float kMinUiScale = 1.0f;
constexpr float kMaxUiScale = 3.0f;

float clampUiScale(float value) {
    return std::clamp(value, kMinUiScale, kMaxUiScale);
}

} // namespace

std::filesystem::path EditorPrefs::prefsFilePath() {
    return editorDataRoot() / "prefs.json";
}

EditorPrefs EditorPrefs::load() {
    EditorPrefs prefs{};
    const std::filesystem::path path = prefsFilePath();
    if (!std::filesystem::exists(path)) {
        return prefs;
    }

    try {
        std::ifstream in(path);
        const nlohmann::json root = nlohmann::json::parse(in);
        if (root.contains("ui_scale") && root["ui_scale"].is_number()) {
            prefs.ui_scale = clampUiScale(root["ui_scale"].get<float>());
        }
        if (root.contains("start_maximized") && root["start_maximized"].is_boolean()) {
            prefs.start_maximized = root["start_maximized"].get<bool>();
        }
        if (root.contains("start_fullscreen") && root["start_fullscreen"].is_boolean()) {
            prefs.start_fullscreen = root["start_fullscreen"].get<bool>();
        }
        if (root.contains("fps_cap_enabled") && root["fps_cap_enabled"].is_boolean()) {
            prefs.fps_cap_enabled = root["fps_cap_enabled"].get<bool>();
        }
        if (root.contains("fps_cap") && root["fps_cap"].is_number()) {
            prefs.fps_cap = std::clamp(root["fps_cap"].get<float>(), 1.0f, 240.0f);
        }
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to read editor prefs ({}): {}", path.string(), ex.what());
    }
    return prefs;
}

void EditorPrefs::save() const {
    ensureEditorDataDirs();
    nlohmann::json root = nlohmann::json::object();
    root["ui_scale"] = clampUiScale(ui_scale);
    root["start_maximized"] = start_maximized;
    root["start_fullscreen"] = start_fullscreen;
    root["fps_cap_enabled"] = fps_cap_enabled;
    root["fps_cap"] = std::clamp(fps_cap, 1.0f, 240.0f);

    const std::filesystem::path path = prefsFilePath();
    std::ofstream out(path);
    if (!out) {
        spdlog::warn("Failed to write editor prefs ({})", path.string());
        return;
    }
    out << root.dump(2);
}

} // namespace genesis
