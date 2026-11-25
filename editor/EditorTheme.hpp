#pragma once

struct ImFont;

namespace genesis {

// Modern realtime/DCC chrome (Unreal/Blender/TD clarity — not WinXP grey boxes).
// ui_scale enlarges paddings/controls (fonts are baked separately in ImGuiLayer).
void applyGenesisEditorTheme(float ui_scale = 2.0f);

// Load Inter / Segoe (+ medium/semibold) into ImGui. Safe before first NewFrame.
ImFont* loadGenesisEditorFonts(float ui_scale = 2.0f);

// Section label used across Hierarchy / Inspector panels.
void drawEditorSectionLabel(const char* label);

} // namespace genesis
