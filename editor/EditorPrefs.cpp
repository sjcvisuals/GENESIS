#include "editor/EditorPrefs.hpp"

#include "platform/EditorPaths.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <fstream>

namespace genesis {

namespace {

constexpr float kMinUiScale = 1.0f;
constexpr float kMaxUiScale = 3.0f;
constexpr float kDefaultUiScale = 1.5f;
// Pre-1.5 product default; migrate exact matches once (see ui_scale_migrated_1_5).
constexpr float kLegacyDefaultUiScale = 2.0f;

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
        bool migrated = false;
        if (root.contains("ui_scale") && root["ui_scale"].is_number()) {
            float scale = root["ui_scale"].get<float>();
            const bool already_migrated =
                root.contains("ui_scale_migrated_1_5") && root["ui_scale_migrated_1_5"].is_boolean() &&
                root["ui_scale_migrated_1_5"].get<bool>();
            // One-shot: factory 2.0 → 1.5. Users who re-save 2.0 after migration keep it.
            if (!already_migrated && std::fabs(scale - kLegacyDefaultUiScale) < 1e-3f) {
                scale = kDefaultUiScale;
                migrated = true;
            }
            prefs.ui_scale = clampUiScale(scale);
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
        if (migrated) {
            prefs.save();
            spdlog::info("Migrated editor ui_scale default 2.0 → 1.5 ({})", path.string());
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
    root["ui_scale_migrated_1_5"] = true;
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
