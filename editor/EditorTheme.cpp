#include "editor/EditorTheme.hpp"

#include <imgui.h>

#include <cstdio>
#include <filesystem>

namespace genesis {

namespace {

bool tryAddFont(ImGuiIO& io, const char* path, float size_px, ImFontConfig* cfg = nullptr) {
    if (!path || !std::filesystem::exists(path)) {
        return false;
    }
    return io.Fonts->AddFontFromFileTTF(path, size_px, cfg) != nullptr;
}

} // namespace

ImFont* loadGenesisEditorFonts() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    // Prefer Inter — clean UI sans used by modern tools. Fall back to Noto / default.
    constexpr float kUi = 16.0f;
    constexpr const char* kInterRegular = "/usr/share/fonts/truetype/macos/Inter-Regular.ttf";
    constexpr const char* kInterMedium = "/usr/share/fonts/truetype/macos/Inter-Medium.ttf";
    constexpr const char* kInterSemi = "/usr/share/fonts/truetype/macos/Inter-SemiBold.ttf";
    constexpr const char* kNoto = "/usr/share/fonts/truetype/noto/NotoSansDisplay-Regular.ttf";

    ImFont* primary = nullptr;
    if (tryAddFont(io, kInterRegular, kUi)) {
        primary = io.Fonts->Fonts.back();
        // Keep Medium/SemiBold in the atlas for future PushFont use (same size).
        tryAddFont(io, kInterMedium, kUi);
        tryAddFont(io, kInterSemi, kUi);
    } else if (tryAddFont(io, kNoto, kUi)) {
        primary = io.Fonts->Fonts.back();
    } else {
        primary = io.Fonts->AddFontDefault();
    }

    io.FontDefault = primary;
    io.FontGlobalScale = 1.0f;
    return primary;
}

void applyGenesisEditorTheme() {
    ImGuiStyle& style = ImGui::GetStyle();

    // Soft-rounded DCC shell — denser than stock ImGui, quieter than neon dashboards.
    style.WindowRounding = 6.0f;
    style.ChildRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 3.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.TabRounding = 4.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    style.WindowPadding = ImVec2(12.0f, 10.0f);
    style.FramePadding = ImVec2(9.0f, 5.0f);
    style.CellPadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 16.0f;
    style.ScrollbarSize = 10.0f;
    style.GrabMinSize = 10.0f;
    style.WindowTitleAlign = ImVec2(0.02f, 0.5f);
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.DisplaySafeAreaPadding = ImVec2(4.0f, 4.0f);
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextPadding = ImVec2(12.0f, 3.0f);
    style.DockingSeparatorSize = 2.0f;
    style.TabBarBorderSize = 0.0f;
    style.TabBarOverlineSize = 2.0f;
    style.HoverStationaryDelay = 0.12f;
    style.HoverDelayNormal = 0.35f;

    ImVec4* c = style.Colors;

    // Cool graphite canvas — deeper than XP grey, warmer than pure black.
    const ImVec4 bg0(0.070f, 0.072f, 0.078f, 1.00f); // app / dock empty
    const ImVec4 bg1(0.098f, 0.102f, 0.112f, 0.98f); // panel
    const ImVec4 bg2(0.122f, 0.128f, 0.140f, 1.00f); // elevated / title
    const ImVec4 bg3(0.148f, 0.156f, 0.170f, 1.00f); // hover surfaces
    const ImVec4 input(0.078f, 0.082f, 0.090f, 1.00f);
    const ImVec4 border(0.180f, 0.190f, 0.205f, 0.85f);
    const ImVec4 text(0.930f, 0.938f, 0.950f, 1.00f);
    const ImVec4 muted(0.520f, 0.545f, 0.585f, 1.00f);
    // Cool cyan-teal accent (professional DCC, not purple glow / neon).
    const ImVec4 accent(0.32f, 0.78f, 0.82f, 1.00f);
    const ImVec4 accent_hi(0.42f, 0.86f, 0.88f, 1.00f);
    const ImVec4 accent_lo(0.18f, 0.42f, 0.46f, 1.00f);
    const ImVec4 accent_soft(0.32f, 0.78f, 0.82f, 0.16f);
    const ImVec4 select(0.32f, 0.78f, 0.82f, 0.28f);

    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = muted;
    c[ImGuiCol_WindowBg] = bg1;
    c[ImGuiCol_ChildBg] = ImVec4(bg0.x, bg0.y, bg0.z, 0.55f);
    c[ImGuiCol_PopupBg] = ImVec4(bg2.x, bg2.y, bg2.z, 0.98f);
    c[ImGuiCol_Border] = border;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = input;
    c[ImGuiCol_FrameBgHovered] = bg3;
    c[ImGuiCol_FrameBgActive] = ImVec4(0.165f, 0.175f, 0.190f, 1.0f);
    c[ImGuiCol_TitleBg] = bg0;
    c[ImGuiCol_TitleBgActive] = bg2;
    c[ImGuiCol_TitleBgCollapsed] = bg0;
    c[ImGuiCol_MenuBarBg] = bg0;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.28f, 0.30f, 0.34f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.38f, 0.40f, 0.44f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive] = accent_lo;
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accent_hi;
    c[ImGuiCol_Button] = bg2;
    c[ImGuiCol_ButtonHovered] = bg3;
    c[ImGuiCol_ButtonActive] = accent_lo;
    c[ImGuiCol_Header] = accent_soft;
    c[ImGuiCol_HeaderHovered] = select;
    c[ImGuiCol_HeaderActive] = ImVec4(0.32f, 0.78f, 0.82f, 0.40f);
    c[ImGuiCol_Separator] = ImVec4(0.160f, 0.170f, 0.185f, 1.0f);
    c[ImGuiCol_SeparatorHovered] = accent_lo;
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ResizeGrip] = ImVec4(0.28f, 0.30f, 0.34f, 0.35f);
    c[ImGuiCol_ResizeGripHovered] = accent_lo;
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_Tab] = bg0;
    c[ImGuiCol_TabHovered] = bg3;
    c[ImGuiCol_TabSelected] = bg1;
    c[ImGuiCol_TabSelectedOverline] = accent;
    c[ImGuiCol_TabDimmed] = bg0;
    c[ImGuiCol_TabDimmedSelected] = bg1;
    c[ImGuiCol_DockingPreview] = ImVec4(0.32f, 0.78f, 0.82f, 0.32f);
    c[ImGuiCol_DockingEmptyBg] = bg0;
    c[ImGuiCol_PlotLines] = accent;
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_TableHeaderBg] = bg2;
    c[ImGuiCol_TableBorderStrong] = border;
    c[ImGuiCol_TableBorderLight] = ImVec4(0.140f, 0.150f, 0.165f, 1.0f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.018f);
    c[ImGuiCol_TextSelectedBg] = select;
    c[ImGuiCol_DragDropTarget] = accent;
    c[ImGuiCol_NavCursor] = accent;
    c[ImGuiCol_NavWindowingHighlight] = accent;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.02f, 0.02f, 0.03f, 0.62f);
}

void drawEditorSectionLabel(const char* label) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.72f, 0.78f, 1.0f));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.18f, 0.28f, 0.30f, 0.85f));
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

} // namespace genesis
