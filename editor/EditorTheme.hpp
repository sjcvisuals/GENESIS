#pragma once

struct ImFont;

namespace genesis {

// Modern realtime/DCC chrome (Unreal/Blender/TD clarity — not WinXP grey boxes).
void applyGenesisEditorTheme();

// Load Inter (+ medium/semibold) into ImGui. Safe before first NewFrame; returns default font.
ImFont* loadGenesisEditorFonts();

// Section label used across Hierarchy / Inspector panels.
void drawEditorSectionLabel(const char* label);

} // namespace genesis
