#include "editor/TugelaEditor.hpp"

#include "editor/EditorTheme.hpp"
#include "particles/ParticleCurve.hpp"
#include "scene/ScenePath.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

#include <imgui.h>

namespace genesis {

namespace {

constexpr ImVec4 kAccent{0.34f, 0.78f, 0.82f, 1.0f};
constexpr ImVec4 kMuted{0.50f, 0.54f, 0.58f, 1.0f};
constexpr ImVec4 kPanel{0.032f, 0.034f, 0.038f, 1.0f};

void collectSystems(Scene& scene, std::vector<NodeId>& out) {
    out.clear();
    for (const Node& n : scene.nodes) {
        if (isParticleSystemNode(scene, n)) {
            out.push_back(n.id);
        }
    }
}

void collectEmitters(Scene& scene, NodeId system_id, std::vector<NodeId>& out) {
    out.clear();
    const Node* system = scene.find(system_id);
    if (!system) {
        return;
    }
    for (const NodeId child : system->children) {
        if (const Node* c = scene.find(child); c && c->particle_emitter.has_value()) {
            out.push_back(child);
        }
    }
}

NodeId resolveSystemForNode(Scene& scene, NodeId id) {
    const Node* n = scene.find(id);
    if (!n) {
        return 0;
    }
    if (isParticleSystemNode(scene, *n)) {
        return id;
    }
    if (n->particle_emitter.has_value() && n->parent.has_value()) {
        if (const Node* parent = scene.find(*n->parent);
            parent && isParticleSystemNode(scene, *parent)) {
            return *n->parent;
        }
    }
    return 0;
}

void drawTimingStrip(ParticleEmitter& e, ParticleEmitterUiContext& ctx) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.022f, 0.024f, 0.028f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
    ImGui::BeginChild("tugela_timings", ImVec2(0.0f, 78.0f), ImGuiChildFlags_Borders);
    ImGui::TextColored(kAccent, "Timings");
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "  ·  emitter clock");
    ImGui::Spacing();

    const float avail = ImGui::GetContentRegionAvail().x;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float cell = (avail - gap * 3.0f) / 4.0f;
    bool dirty = false;

    ImGui::PushItemWidth(cell - 4.0f);
    dirty |= ImGui::DragFloat("Duration", &e.duration_seconds, 0.05f, 0.0f, 600.0f, "%.2f");
    ImGui::SameLine();
    dirty |= ImGui::DragFloat("Delay", &e.start_delay_seconds, 0.01f, 0.0f, 60.0f, "%.2f");
    ImGui::SameLine();
    dirty |= ImGui::DragFloat("Life min", &e.lifetime_min, 0.01f, 0.01f, 60.0f, "%.2f");
    e.lifetime_max = std::max(e.lifetime_max, e.lifetime_min);
    ImGui::SameLine();
    dirty |= ImGui::DragFloat("Rate / s", &e.rate_per_second, 0.5f, 0.0f, 20000.0f, "%.1f");
    ImGui::PopItemWidth();

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    if (dirty && ctx.mark_dirty) {
        ctx.mark_dirty();
    }
}

void drawLeanSizeCurve(ParticleCurve1& curve, bool& dirty) {
    ImGui::TextColored(kMuted, "Size over life");
    float samples[32];
    const int n = 32;
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n - 1);
        samples[i] = curve.evaluate(t);
    }
    ImGui::PushStyleColor(ImGuiCol_PlotLines, kAccent);
    ImGui::PlotLines("##size_curve", samples, n, 0, nullptr, 0.0f, 2.0f, ImVec2(-1.0f, 56.0f));
    ImGui::PopStyleColor();

    int keys = static_cast<int>(curve.count);
    if (ImGui::DragInt("Size keys", &keys, 1, 2, 4)) {
        curve.count = static_cast<std::uint8_t>(std::clamp(keys, 2, 4));
        dirty = true;
    }
    for (std::uint8_t i = 0; i < curve.count; ++i) {
        ImGui::PushID(static_cast<int>(300 + i));
        ImGui::SetNextItemWidth(72.0f);
        dirty |= ImGui::DragFloat("t", &curve.keys[i].t, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        dirty |= ImGui::DragFloat("mul", &curve.keys[i].v, 0.01f, 0.0f, 4.0f, "%.2f");
        ImGui::PopID();
    }
}

void drawLeanColorCurve(ParticleCurveColor& curve, bool& dirty) {
    ImGui::TextColored(kMuted, "Color over life");
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = 18.0f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const int segs = 48;
    for (int i = 0; i < segs; ++i) {
        const float t0 = static_cast<float>(i) / static_cast<float>(segs);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(segs);
        const glm::vec4 c0 = curve.evaluate(t0);
        const glm::vec4 c1 = curve.evaluate(t1);
        const ImU32 col0 = ImGui::ColorConvertFloat4ToU32(ImVec4(c0.r, c0.g, c0.b, c0.a));
        const ImU32 col1 = ImGui::ColorConvertFloat4ToU32(ImVec4(c1.r, c1.g, c1.b, c1.a));
        draw->AddRectFilledMultiColor(
            ImVec2(p0.x + width * t0, p0.y), ImVec2(p0.x + width * t1, p0.y + height), col0, col1,
            col1, col0);
    }
    draw->AddRect(p0, ImVec2(p0.x + width, p0.y + height),
                  IM_COL32(70, 80, 90, 180), 3.0f, 0, 1.0f);
    ImGui::Dummy(ImVec2(width, height + 4.0f));

    int keys = static_cast<int>(curve.count);
    if (ImGui::DragInt("Color keys", &keys, 1, 2, 4)) {
        curve.count = static_cast<std::uint8_t>(std::clamp(keys, 2, 4));
        dirty = true;
    }
    for (std::uint8_t i = 0; i < curve.count; ++i) {
        ImGui::PushID(static_cast<int>(400 + i));
        ImGui::SetNextItemWidth(72.0f);
        dirty |= ImGui::DragFloat("t##c", &curve.keys[i].t, 0.01f, 0.0f, 1.0f, "%.2f");
        dirty |= ImGui::ColorEdit4("rgba", &curve.keys[i].v.x,
                                   ImGuiColorEditFlags_AlphaPreviewHalf |
                                       ImGuiColorEditFlags_NoInputs);
        ImGui::PopID();
    }
}

void drawCurvePanel(ParticleEmitter& e, ParticleEmitterUiContext& ctx) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanel);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::BeginChild("tugela_curves", ImVec2(0.0f, 210.0f), ImGuiChildFlags_Borders);
    ImGui::TextColored(kAccent, "Curves");
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "  ·  lean over-life graphs");
    ImGui::Spacing();

    bool dirty = false;
    if (ImGui::Button("Rebuild from endpoints")) {
        rebuildEmitterCurvesFromEndpoints(e);
        dirty = true;
    }
    ImGui::Spacing();
    drawLeanSizeCurve(e.size_over_life, dirty);
    ImGui::Spacing();
    drawLeanColorCurve(e.color_over_life, dirty);

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    if (dirty) {
        syncEmitterCurvesFromEndpoints(e);
        if (ctx.mark_dirty) {
            ctx.mark_dirty();
        }
    }
}

bool selectableRow(const char* label, bool selected, const char* badge) {
    const ImGuiSelectableFlags flags = ImGuiSelectableFlags_AllowDoubleClick;
    const bool clicked = ImGui::Selectable(label, selected, flags);
    if (badge && badge[0] != '\0') {
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 36.0f);
        ImGui::TextColored(kMuted, "%s", badge);
    }
    return clicked;
}

} // namespace

void tugelaEditorFocusSelection(TugelaEditorState& state, Scene& scene, NodeId selected) {
    const NodeId system = resolveSystemForNode(scene, selected);
    if (system == 0) {
        return;
    }
    // Sync focus when the window is already open (View menu / T opens it explicitly).
    if (!state.open) {
        return;
    }
    state.focus_system = system;
    if (const Node* n = scene.find(selected); n && n->particle_emitter.has_value()) {
        state.focus_emitter = selected;
    } else {
        std::vector<NodeId> emitters;
        collectEmitters(scene, system, emitters);
        state.focus_emitter = emitters.empty() ? 0 : emitters.front();
    }
}

void drawTugelaEditorWindow(TugelaEditorState& state, ParticleEmitterUiContext& ctx) {
    if (!state.open || !ctx.scene) {
        return;
    }
    Scene& scene = *ctx.scene;

    ImGui::SetNextWindowSize(ImVec2(980.0f, 640.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(720.0f, 420.0f), ImVec2(2400.0f, 1600.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.038f, 0.040f, 0.046f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.024f, 0.026f, 0.030f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.030f, 0.036f, 0.042f, 1.0f));

    if (!ImGui::Begin("Tugela Editor", &state.open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);
        return;
    }
    ImGui::PopStyleVar(2);

    // Header bar
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        draw->AddRectFilled(p, ImVec2(p.x + w, p.y + 36.0f), IM_COL32(18, 20, 24, 255));
        draw->AddRectFilled(p, ImVec2(p.x + 3.0f, p.y + 36.0f), IM_COL32(86, 198, 208, 255));
        ImGui::Dummy(ImVec2(w, 36.0f));
        ImGui::SetCursorScreenPos(ImVec2(p.x + 14.0f, p.y + 9.0f));
        ImGui::TextColored(kAccent, "Tugela");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.88f, 0.90f, 0.93f, 1.0f), "  System · Emitter · Modules");
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "   lean authoring — no node graph");
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 40.0f));
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
    ImGui::BeginChild("tugela_body", ImVec2(0, 0), ImGuiChildFlags_None);

    std::vector<NodeId> systems;
    collectSystems(scene, systems);
    if (state.focus_system == 0 && !systems.empty()) {
        state.focus_system = systems.front();
    }
    if (std::find(systems.begin(), systems.end(), state.focus_system) == systems.end()) {
        state.focus_system = systems.empty() ? 0 : systems.front();
    }

    std::vector<NodeId> emitters;
    if (state.focus_system != 0) {
        collectEmitters(scene, state.focus_system, emitters);
    }
    if (state.focus_emitter == 0 && !emitters.empty()) {
        state.focus_emitter = emitters.front();
    }
    if (std::find(emitters.begin(), emitters.end(), state.focus_emitter) == emitters.end()) {
        state.focus_emitter = emitters.empty() ? 0 : emitters.front();
    }

    // Left: systems
    ImGui::BeginChild("tugela_systems", ImVec2(168.0f, 0.0f), ImGuiChildFlags_Borders);
    drawEditorSectionLabel("Systems");
    if (systems.empty()) {
        ImGui::TextDisabled("No systems.\nCreate via Hierarchy.");
    }
    for (const NodeId id : systems) {
        const Node* n = scene.find(id);
        if (!n) {
            continue;
        }
        ImGui::PushID(static_cast<int>(id));
        if (selectableRow(n->name.c_str(), state.focus_system == id, "SYS")) {
            state.focus_system = id;
            collectEmitters(scene, id, emitters);
            state.focus_emitter = emitters.empty() ? 0 : emitters.front();
            if (ctx.select_node) {
                ctx.select_node(id);
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Mid: emitters
    ImGui::BeginChild("tugela_emitters", ImVec2(168.0f, 0.0f), ImGuiChildFlags_Borders);
    drawEditorSectionLabel("Emitters");
    if (emitters.empty()) {
        ImGui::TextDisabled("No emitters.");
    }
    for (const NodeId id : emitters) {
        const Node* n = scene.find(id);
        if (!n) {
            continue;
        }
        ImGui::PushID(static_cast<int>(id + 100000));
        const char* badge = n->particle_emitter.has_value() && n->particle_emitter->enabled ? "on" : "off";
        if (selectableRow(n->name.c_str(), state.focus_emitter == id, badge)) {
            state.focus_emitter = id;
            if (ctx.select_node) {
                ctx.select_node(id);
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right: timings + curves + modules
    ImGui::BeginChild("tugela_detail", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
    Node* emitter = scene.find(state.focus_emitter);
    if (!emitter || !emitter->particle_emitter.has_value()) {
        ImGui::TextDisabled("Select an emitter to edit modules, timings, and curves.");
    } else {
        ParticleEmitter& e = *emitter->particle_emitter;
        const std::string path = nodePath(scene, emitter->id);

        ImGui::TextColored(ImVec4(0.90f, 0.92f, 0.94f, 1.0f), "%s", emitter->name.c_str());
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "  emitter");
        ImGui::Spacing();

        drawTimingStrip(e, ctx);
        ImGui::Spacing();
        drawCurvePanel(e, ctx);
        ImGui::Spacing();

        ImGui::BeginChild("tugela_modules", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
        drawEditorSectionLabel("Modules");
        drawParticleEmitterInspector(*emitter, path, ctx);
        ImGui::EndChild();
    }
    ImGui::EndChild();

    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::End();
    ImGui::PopStyleColor(3);
}

} // namespace genesis
