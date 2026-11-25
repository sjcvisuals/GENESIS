#pragma once

#include <filesystem>

namespace genesis {

// Persistent editor preferences (SDL pref path / prefs.json).
struct EditorPrefs {
    // Multiplier for ImGui fonts + style sizes. 1.5 reads clearly on Windows.
    float ui_scale = 1.5f;
    // Open the main window maximized (full monitor work area).
    bool start_maximized = true;
    // When true, also request borderless fullscreen after create (optional).
    bool start_fullscreen = false;

    static EditorPrefs load();
    void save() const;

    static std::filesystem::path prefsFilePath();
};

} // namespace genesis
