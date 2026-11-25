#include "editor/PerfDebugUi.hpp"

#include <imgui.h>

namespace genesis {

void drawPerfDebugOverlayWindow(const PerfDebugSnapshot& snap, bool* open) {
    ImGui::SetNextWindowPos(ImVec2(16.0f, 48.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav;
    if (!ImGui::Begin("Performance", open, flags)) {
        ImGui::End();
        return;
    }

    ImGui::Text("FPS  %.1f", snap.fps);
    ImGui::Text("Frame %.2f ms", snap.frame_ms);
    ImGui::Indent();
    ImGui::Text("Update %.2f ms", snap.update_ms);
    ImGui::Text("UI     %.2f ms", snap.imgui_ms);
    ImGui::Text("Draw   %.2f ms", snap.draw_ms);
    ImGui::Unindent();
    ImGui::Separator();
    ImGui::Text("Draw calls  %u", snap.draw_calls);
    ImGui::Text("Triangles   %u", snap.triangles);
    ImGui::Text("Meshes      %u  /  nodes %u", snap.visible_mesh_nodes, snap.scene_nodes);
    ImGui::Text("Lights      %u", snap.active_lights);
    ImGui::Separator();
    ImGui::Text("Particles   %u alive", snap.particles_alive);
    ImGui::Text("Emitters    %u", snap.particle_emitters);
    ImGui::Text("Sim steps   %u  (%s)", snap.particle_steps, snap.particle_gpu ? "GPU" : "CPU");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Fixed 10 ms particle steps from wall-clock engine dt.\n"
            "Frame 0.00 ms + frozen FPS used to mean 1 ms timer quantization\n"
            "(particles pulsed while FPS never 'dropped'). Cap FPS A/B still useful.");
    }
    ImGui::Separator();
    ImGui::TextWrapped("GPU  %s", snap.gpu_name[0] ? snap.gpu_name : "(unknown)");
    ImGui::Text("%u × %u", snap.width, snap.height);
    ImGui::End();
}

void drawPerfDebugControls(bool& show_overlay, bool& show_on_output) {
    ImGui::Checkbox("Show overlay", &show_overlay);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Viewport performance overlay (editor).");
    }
    ImGui::Checkbox("Show on Renderbridge output", &show_on_output);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Burn the same stats into Pixera Renderbridge / offscreen output when enabled.");
    }
}

} // namespace genesis
