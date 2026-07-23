#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

namespace genesis {

inline bool centralDockViewportRect(const ImGuiID dockspace_id, ImVec2& out_pos, ImVec2& out_size) {
    ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dockspace_id);
    if (!central) {
        return false;
    }

    out_pos = central->Pos;
    out_size = central->Size;
    return central->Size.x > 1.0f && central->Size.y > 1.0f;
}

inline void beginEditorDockSpace(ImGuiID dockspace_id, bool& first_time_setup) {
    const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(main_viewport->WorkPos);
    ImGui::SetNextWindowSize(main_viewport->WorkSize);
    ImGui::SetNextWindowViewport(main_viewport->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##GenesisDockHost", nullptr, flags);
    ImGui::PopStyleVar(3);

    // Central node stays empty so the 3D render shows through (passthrough).
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    if (first_time_setup) {
        first_time_setup = false;

        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, main_viewport->WorkSize);

        ImGuiID dock_main = dockspace_id;
        const ImGuiID dock_left = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.20f, nullptr, &dock_main);
        const ImGuiID dock_right =
            ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.24f, nullptr, &dock_main);
        const ImGuiID dock_bottom =
            ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.08f, nullptr, &dock_main);

        ImGui::DockBuilderDockWindow("Hierarchy", dock_left);
        ImGui::DockBuilderDockWindow("Inspector", dock_right);
        ImGui::DockBuilderDockWindow("Toolbar", dock_bottom);
        // Protocol / Timeline stay available via View menu — keep first launch uncluttered.
        // Leave dock_main (center) empty — scene renders in the passthrough hole.
        ImGui::DockBuilderFinish(dockspace_id);
    }

    ImGui::End();
}

template <typename DrawGizmoFn>
inline void drawViewportInputOverlay(const ImGuiID dockspace_id, bool enabled, ImVec2& out_rect_min,
                                     ImVec2& out_rect_max, bool& out_hovered, bool& out_input_active,
                                     DrawGizmoFn&& draw_gizmo) {
    out_hovered = false;
    out_input_active = false;
    if (!enabled) {
        return;
    }

    ImVec2 pos{};
    ImVec2 size{};
    if (!centralDockViewportRect(dockspace_id, pos, size)) {
        return;
    }

    out_rect_min = pos;
    out_rect_max = ImVec2(pos.x + size.x, pos.y + size.y);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::Begin(
        "##ViewportOverlay", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);

    // ImGuizmo treats this overlay as the viewport so arrows receive hover/drag input.
    ImGuizmo::SetAlternativeWindow(ImGui::GetCurrentWindow());

    // Manipulate first — ImGuizmo::CanActivate fails if another item is already active/hovered.
    draw_gizmo();

    // Keep a hit target when the gizmo is idle so the overlay window receives focus/clicks.
    // While using/hovering the gizmo, skip the button so Manipulate keeps mouse ownership.
    if (!ImGuizmo::IsUsing() && !ImGuizmo::IsOver()) {
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton("##ViewportInput", size);
        out_input_active = ImGui::IsItemHovered() || ImGui::IsItemActive();
    }
    out_hovered = ImGui::IsMouseHoveringRect(out_rect_min, out_rect_max, false);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ImGui::GetForegroundDrawList()->AddRect(out_rect_min, out_rect_max, IM_COL32(28, 30, 34, 180), 0.0f, 0,
                                            1.0f);
}

} // namespace genesis
