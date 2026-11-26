#pragma once

#include <imgui.h>

#include <algorithm>

namespace genesis {

// Place the next widget on the same line only when `needed_width` still fits.
// Prevents label/control clashes when docked panels shrink.
inline bool uiSameLineIfFits(float needed_width, float spacing = -1.0f) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float space = spacing < 0.0f ? style.ItemSpacing.x : spacing;
    if (ImGui::GetContentRegionAvail().x < needed_width + space) {
        return false;
    }
    ImGui::SameLine(0.0f, spacing);
    return true;
}

// Equal-width columns for a wrapping button row (clamped so labels stay usable).
inline float uiEqualColumnWidth(int columns) {
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float full = ImGui::GetContentRegionAvail().x;
    const int cols = std::max(columns, 1);
    return std::max(1.0f, (full - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols));
}

// Prefer N equal columns when the panel is wide enough for the longest label; else stack full-width.
inline int uiAdaptiveColumns(int prefer_columns, float min_label_width) {
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float full = ImGui::GetContentRegionAvail().x;
    const float pad = ImGui::GetStyle().FramePadding.x * 2.0f;
    const float cell = min_label_width + pad + 4.0f;
    for (int cols = prefer_columns; cols >= 2; --cols) {
        if (full >= cell * static_cast<float>(cols) + gap * static_cast<float>(cols - 1)) {
            return cols;
        }
    }
    return 1;
}

inline float uiButtonWidthForColumns(int columns) {
    return columns <= 1 ? -1.0f : uiEqualColumnWidth(columns);
}

} // namespace genesis
