#include "editor/TugelaEditor.hpp"

#include "editor/EditorTheme.hpp"
#include "particles/ParticleCurve.hpp"
#include "scene/ScenePath.hpp"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <vector>

#include <imgui.h>

namespace genesis {

namespace {

constexpr ImVec4 kAccent{0.34f, 0.78f, 0.82f, 1.0f};
constexpr ImVec4 kMuted{0.50f, 0.54f, 0.58f, 1.0f};
constexpr ImVec4 kPanel{0.032f, 0.034f, 0.038f, 1.0f};

constexpr float kNodeW = 102.0f;
constexpr float kNodeH = 48.0f;
constexpr float kNodeGapX = 18.0f;
constexpr float kEmitterRowH = 100.0f;

const char* moduleLabel(TugelaModuleKind kind) {
    switch (kind) {
    case TugelaModuleKind::Spawn:
        return "Spawn";
    case TugelaModuleKind::Initialize:
        return "Initialize";
    case TugelaModuleKind::Update:
        return "Update";
    case TugelaModuleKind::OverLife:
        return "Over Life";
    case TugelaModuleKind::Sprite:
        return "Sprite";
    case TugelaModuleKind::Trail:
        return "Trail";
    default:
        return "Module";
    }
}

const char* moduleSubtitle(TugelaModuleKind kind, const ParticleEmitter& e) {
    switch (kind) {
    case TugelaModuleKind::Spawn:
        return e.rate_per_second > 0.0f ? "rate" : "burst";
    case TugelaModuleKind::Initialize:
        return "life · size";
    case TugelaModuleKind::Update:
        return e.force_gravity ? "lift on" : "forces";
    case TugelaModuleKind::OverLife:
        return "curves";
    case TugelaModuleKind::Sprite:
        return e.additive ? "additive" : "sprite";
    case TugelaModuleKind::Trail:
        return e.trail_enabled ? "on" : "off";
    default:
        return "";
    }
}

bool moduleActive(TugelaModuleKind kind, const ParticleEmitter& e) {
    switch (kind) {
    case TugelaModuleKind::Trail:
        return e.trail_enabled;
    case TugelaModuleKind::Update:
        return e.force_gravity || e.force_drag || e.force_vortex || e.force_point || e.force_noise ||
               e.force_accel;
    case TugelaModuleKind::Sprite:
        return e.render_mesh == ParticleRenderMesh::Sprite;
    default:
        return true;
    }
}

void toggleModule(TugelaModuleKind kind, ParticleEmitter& e) {
    switch (kind) {
    case TugelaModuleKind::Trail:
        e.trail_enabled = !e.trail_enabled;
        break;
    case TugelaModuleKind::Update:
        e.force_gravity = !e.force_gravity;
        break;
    case TugelaModuleKind::Sprite:
        e.additive = !e.additive;
        break;
    default:
        break;
    }
}

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
        // Orphan emitter under any empty parent → treat parent as system actor.
        if (const Node* parent = scene.find(*n->parent);
            parent && !parent->particle_emitter.has_value() && !parent->mesh_resource.has_value() &&
            !parent->camera.has_value() && !parent->light.has_value()) {
            return *n->parent;
        }
    }
    return 0;
}

void mark(ParticleEmitterUiContext& ctx) {
    if (ctx.mark_dirty) {
        ctx.mark_dirty();
    }
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

    if (dirty) {
        mark(ctx);
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
        draw->AddRectFilledMultiColor(ImVec2(p0.x + width * t0, p0.y),
                                      ImVec2(p0.x + width * t1, p0.y + height), col0, col1, col1,
                                      col0);
    }
    draw->AddRect(p0, ImVec2(p0.x + width, p0.y + height), IM_COL32(70, 80, 90, 180), 3.0f, 0,
                  1.0f);
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
        mark(ctx);
    }
}

bool selectableRow(const char* label, bool selected, const char* badge) {
    const bool clicked = ImGui::Selectable(label, selected, ImGuiSelectableFlags_AllowDoubleClick);
    if (badge && badge[0] != '\0') {
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 36.0f);
        ImGui::TextColored(kMuted, "%s", badge);
    }
    return clicked;
}

void drawModuleInspector(ParticleEmitter& e, TugelaModuleKind kind, ParticleEmitterUiContext& ctx) {
    bool dirty = false;
    ImGui::TextColored(kAccent, "%s", moduleLabel(kind));
    ImGui::Spacing();
    switch (kind) {
    case TugelaModuleKind::Spawn:
        dirty |= ImGui::DragFloat("Rate / s", &e.rate_per_second, 0.5f, 0.0f, 20000.0f);
        dirty |= ImGui::DragFloat("Spawn / meter", &e.spawn_per_unit, 0.1f, 0.0f, 500.0f);
        {
            int burst = static_cast<int>(e.burst_count);
            if (ImGui::DragInt("Burst @ t=0", &burst, 1, 0, 100000)) {
                e.burst_count = static_cast<std::uint32_t>(std::max(burst, 0));
                dirty = true;
            }
        }
        break;
    case TugelaModuleKind::Initialize:
        dirty |= ImGui::DragFloatRange2("Lifetime", &e.lifetime_min, &e.lifetime_max, 0.01f, 0.01f,
                                        60.0f);
        dirty |= ImGui::DragFloatRange2("Speed", &e.speed_min, &e.speed_max, 0.01f, 0.0f, 100.0f);
        dirty |= ImGui::DragFloatRange2("Size", &e.size_min, &e.size_max, 0.001f, 0.02f, 10.0f);
        dirty |= ImGui::ColorEdit4("Color start", &e.color_start.x,
                                   ImGuiColorEditFlags_AlphaPreviewHalf);
        dirty |=
            ImGui::ColorEdit4("Color end", &e.color_end.x, ImGuiColorEditFlags_AlphaPreviewHalf);
        break;
    case TugelaModuleKind::Update:
        dirty |= ImGui::Checkbox("Gravity / Lift", &e.force_gravity);
        if (e.force_gravity) {
            dirty |= ImGui::DragFloat3("Gravity (+Y = lift)", &e.gravity.x, 0.02f);
        }
        dirty |= ImGui::Checkbox("Drag", &e.force_drag);
        if (e.force_drag) {
            dirty |= ImGui::DragFloat("Drag amount", &e.drag, 0.01f, 0.0f, 10.0f);
        }
        dirty |= ImGui::Checkbox("Vortex / Orbit", &e.force_vortex);
        break;
    case TugelaModuleKind::OverLife:
        dirty |= ImGui::DragFloat("Size end mul", &e.size_end_multiplier, 0.01f, 0.0f, 2.0f);
        if (ImGui::Button("Rebuild curves from endpoints")) {
            rebuildEmitterCurvesFromEndpoints(e);
            dirty = true;
        }
        break;
    case TugelaModuleKind::Sprite:
        dirty |= ImGui::Checkbox("Unlit", &e.unlit);
        dirty |= ImGui::Checkbox("Soft additive", &e.additive);
        dirty |= ImGui::DragFloat("Sprite aspect", &e.sprite_aspect, 0.01f, 0.05f, 8.0f);
        break;
    case TugelaModuleKind::Trail:
        dirty |= ImGui::Checkbox("Enable trails", &e.trail_enabled);
        if (e.trail_enabled) {
            int segs = static_cast<int>(e.trail_segments);
            if (ImGui::DragInt("Segments", &segs, 1, 2, 24)) {
                e.trail_segments = static_cast<std::uint8_t>(std::clamp(segs, 2, 24));
                dirty = true;
            }
            dirty |= ImGui::DragFloat("Width (head)", &e.trail_width, 0.001f, 0.01f, 1.0f);
            dirty |= ImGui::DragFloat("Width (tip)", &e.trail_width_end, 0.001f, 0.0f, 1.0f);
            dirty |= ImGui::DragFloat("Trail alpha", &e.trail_alpha, 0.01f, 0.0f, 1.0f);
        }
        break;
    default:
        break;
    }
    if (dirty) {
        syncEmitterCurvesFromEndpoints(e);
        mark(ctx);
        if (ctx.reset_particles) {
            // leave running; mark only
        }
    }
}

bool pointInRect(ImVec2 p, ImVec2 a, ImVec2 b) {
    return p.x >= a.x && p.x <= b.x && p.y >= a.y && p.y <= b.y;
}

// Returns true if a module node under the mouse was selected / toggled.
bool drawEmitterGraphRow(TugelaEditorState& state, Node& emitter_node, int row_index,
                         ParticleEmitterUiContext& ctx, ImDrawList* draw, ImVec2 canvas_origin,
                         bool allow_pick) {
    ParticleEmitter& e = *emitter_node.particle_emitter;
    const float y =
        canvas_origin.y + state.graph_pan_y + static_cast<float>(row_index) * kEmitterRowH;
    const float x0 = canvas_origin.x + state.graph_pan_x;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    bool picked = false;

    draw->AddText(ImVec2(x0, y - 18.0f), IM_COL32(140, 150, 160, 255), emitter_node.name.c_str());
    if (!e.enabled) {
        draw->AddText(ImVec2(x0 + ImGui::CalcTextSize(emitter_node.name.c_str()).x + 8.0f, y - 18.0f),
                      IM_COL32(120, 90, 90, 220), "off");
    }

    ImVec2 prev_center{};
    bool has_prev = false;
    for (int mi = 0; mi < static_cast<int>(TugelaModuleKind::Count); ++mi) {
        const auto kind = static_cast<TugelaModuleKind>(mi);
        const float x = x0 + static_cast<float>(mi) * (kNodeW + kNodeGapX);
        const ImVec2 p0(x, y);
        const ImVec2 p1(x + kNodeW, y + kNodeH);
        const bool selected =
            state.focus_emitter == emitter_node.id && state.focus_module == kind;
        const bool active = moduleActive(kind, e);
        const bool hovered = pointInRect(mouse, p0, p1);

        const ImU32 fill = active ? (selected ? IM_COL32(28, 62, 68, 255) : IM_COL32(24, 42, 48, 255))
                                  : IM_COL32(28, 28, 32, 255);
        const ImU32 border =
            selected ? IM_COL32(100, 210, 220, 255)
                     : (hovered ? IM_COL32(90, 150, 160, 230)
                                : (active ? IM_COL32(70, 110, 120, 220) : IM_COL32(55, 58, 64, 200)));
        draw->AddRectFilled(p0, p1, fill, 6.0f);
        draw->AddRect(p0, p1, border, 6.0f, 0, selected || hovered ? 2.0f : 1.0f);
        draw->AddText(ImVec2(p0.x + 10.0f, p0.y + 10.0f), IM_COL32(230, 235, 240, 255),
                      moduleLabel(kind));
        draw->AddText(ImVec2(p0.x + 10.0f, p0.y + 28.0f), IM_COL32(130, 145, 155, 255),
                      moduleSubtitle(kind, e));

        const ImVec2 center((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
        if (has_prev) {
            const ImVec2 a(prev_center.x + kNodeW * 0.5f, prev_center.y);
            const ImVec2 b(center.x - kNodeW * 0.5f, center.y);
            const ImU32 wire = active ? IM_COL32(70, 160, 170, 200) : IM_COL32(60, 65, 72, 160);
            draw->AddBezierCubic(a, ImVec2(a.x + 18.0f, a.y), ImVec2(b.x - 18.0f, b.y), b, wire,
                                 2.0f);
        }
        prev_center = center;
        has_prev = true;

        if (hovered) {
            ImGui::SetTooltip("%s · click select · Ctrl+click toggle", moduleLabel(kind));
        }
        if (allow_pick && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            state.focus_emitter = emitter_node.id;
            state.focus_module = kind;
            if (ctx.select_node) {
                ctx.select_node(emitter_node.id);
            }
            if (ImGui::GetIO().KeyCtrl) {
                toggleModule(kind, e);
                mark(ctx);
            }
            picked = true;
        }
        if (allow_pick && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            state.focus_emitter = emitter_node.id;
            state.focus_module = kind;
            ImGui::OpenPopup("tugela_node_ctx");
        }
    }

    if (ImGui::BeginPopup("tugela_node_ctx")) {
        ImGui::TextDisabled("%s / %s", emitter_node.name.c_str(),
                            moduleLabel(state.focus_module));
        if (ImGui::MenuItem("Select module")) {
            if (ctx.select_node) {
                ctx.select_node(emitter_node.id);
            }
        }
        if (state.focus_module == TugelaModuleKind::Trail ||
            state.focus_module == TugelaModuleKind::Update ||
            state.focus_module == TugelaModuleKind::Sprite) {
            if (ImGui::MenuItem(moduleActive(state.focus_module, e) ? "Disable module"
                                                                    : "Enable module")) {
                toggleModule(state.focus_module, e);
                mark(ctx);
            }
        }
        ImGui::EndPopup();
    }
    return picked;
}

void drawModuleGraph(TugelaEditorState& state, Scene& scene, const std::vector<NodeId>& emitters,
                     ParticleEmitterUiContext& ctx) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.018f, 0.019f, 0.022f, 1.0f));
    ImGui::BeginChild("tugela_graph_canvas", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);

    ImGui::TextColored(kAccent, "Module graph");
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "  ·  one System actor · multiple Emitters · Ctrl+click toggles");
    ImGui::Spacing();

    const ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    if (canvas_size.y < 160.0f) {
        canvas_size.y = 160.0f;
    }

    ImGui::InvisibleButton("##graph_bg", canvas_size);
    const bool canvas_hovered = ImGui::IsItemHovered();
    const bool canvas_active = ImGui::IsItemActive();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(canvas_p0, ImVec2(canvas_p0.x + canvas_size.x, canvas_p0.y + canvas_size.y),
                        IM_COL32(14, 15, 17, 255), 4.0f);

    for (float gx = std::fmod(state.graph_pan_x, 40.0f); gx < canvas_size.x; gx += 40.0f) {
        draw->AddLine(ImVec2(canvas_p0.x + gx, canvas_p0.y),
                      ImVec2(canvas_p0.x + gx, canvas_p0.y + canvas_size.y),
                      IM_COL32(30, 34, 38, 120));
    }
    for (float gy = std::fmod(state.graph_pan_y, 40.0f); gy < canvas_size.y; gy += 40.0f) {
        draw->AddLine(ImVec2(canvas_p0.x, canvas_p0.y + gy),
                      ImVec2(canvas_p0.x + canvas_size.x, canvas_p0.y + gy),
                      IM_COL32(30, 34, 38, 120));
    }

    draw->PushClipRect(canvas_p0, ImVec2(canvas_p0.x + canvas_size.x, canvas_p0.y + canvas_size.y),
                       true);

    if (emitters.empty()) {
        draw->AddText(ImVec2(canvas_p0.x + 24.0f, canvas_p0.y + 24.0f), IM_COL32(120, 130, 140, 255),
                      "Add emitters to this System to build the graph.");
    }

    bool picked_node = false;
    int row = 0;
    for (const NodeId id : emitters) {
        Node* n = scene.find(id);
        if (!n || !n->particle_emitter.has_value()) {
            continue;
        }
        ImGui::PushID(static_cast<int>(id));
        picked_node |=
            drawEmitterGraphRow(state, *n, row, ctx, draw, canvas_p0, canvas_hovered);
        ImGui::PopID();
        ++row;
    }
    draw->PopClipRect();

    if (!picked_node && canvas_active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        state.graph_pan_x += ImGui::GetIO().MouseDelta.x;
        state.graph_pan_y += ImGui::GetIO().MouseDelta.y;
    } else if (canvas_hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        state.graph_pan_x += ImGui::GetIO().MouseDelta.x;
        state.graph_pan_y += ImGui::GetIO().MouseDelta.y;
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void drawAddEmitterButtons(Scene& scene, NodeId system_id, ParticleEmitterUiContext& ctx,
                           TugelaEditorState& state) {
    Node* system = scene.find(system_id);
    if (!system) {
        return;
    }
    ImGui::TextDisabled("Add to System");
    const float full = ImGui::GetContentRegionAvail().x;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float third = (full - gap * 2.0f) / 3.0f;
    auto add = [&](const char* name, const ParticleEmitter& preset) {
        const NodeId id = scene.createParticleEmitterNode(name, system_id, preset);
        mark(ctx);
        state.focus_emitter = id;
        if (ctx.select_node) {
            ctx.select_node(id);
        }
        if (ctx.reset_particles) {
            ctx.reset_particles();
        }
    };
    if (ImGui::Button("Soft Circles", ImVec2(third, 0))) {
        add("Soft Circles", makeGentleCirclesEmitter());
    }
    ImGui::SameLine();
    if (ImGui::Button("Trail", ImVec2(third, 0))) {
        ParticleEmitter trail = makeGentleSoftEmitter();
        trail.rate_per_second = 6.0f;
        trail.trail_enabled = true;
        trail.force_gravity = true;
        add("Trail", trail);
    }
    ImGui::SameLine();
    if (ImGui::Button("Circles+Trail", ImVec2(third, 0))) {
        add("Soft Circles", makeGentleCirclesEmitter());
        ParticleEmitter trail = makeGentleSoftEmitter();
        trail.rate_per_second = 5.0f;
        trail.trail_enabled = true;
        add("Trail", trail);
    }
    (void)system;
}

} // namespace

void tugelaEditorFocusSelection(TugelaEditorState& state, Scene& scene, NodeId selected) {
    const NodeId system = resolveSystemForNode(scene, selected);
    if (system == 0) {
        return;
    }
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

    ImGui::SetNextWindowSize(ImVec2(1180.0f, 720.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(900.0f, 520.0f), ImVec2(2600.0f, 1800.0f));
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
        ImGui::TextColored(ImVec4(0.88f, 0.90f, 0.93f, 1.0f), "  System actor · Emitters · Graph");
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "   lean Niagara-like modules");
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

    // Left: systems (one level actor each)
    ImGui::BeginChild("tugela_systems", ImVec2(150.0f, 0.0f), ImGuiChildFlags_Borders);
    drawEditorSectionLabel("Systems");
    ImGui::TextDisabled("1 actor = N emitters");
    ImGui::Spacing();
    if (systems.empty()) {
        ImGui::TextDisabled("No systems.\nCreate via Hierarchy.");
    }
    for (const NodeId id : systems) {
        const Node* n = scene.find(id);
        if (!n) {
            continue;
        }
        ImGui::PushID(static_cast<int>(id));
        std::vector<NodeId> sys_emitters;
        collectEmitters(scene, id, sys_emitters);
        char badge[16];
        std::snprintf(badge, sizeof(badge), "%zu", sys_emitters.size());
        if (selectableRow(n->name.c_str(), state.focus_system == id, badge)) {
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

    // Mid: emitters in focused system
    ImGui::BeginChild("tugela_emitters", ImVec2(158.0f, 0.0f), ImGuiChildFlags_Borders);
    drawEditorSectionLabel("Emitters");
    if (state.focus_system != 0) {
        if (const Node* sys = scene.find(state.focus_system)) {
            ImGui::TextColored(kMuted, "in %s", sys->name.c_str());
        }
    }
    ImGui::Spacing();
    if (emitters.empty()) {
        ImGui::TextDisabled("Empty system.");
    }
    for (const NodeId id : emitters) {
        const Node* n = scene.find(id);
        if (!n) {
            continue;
        }
        ImGui::PushID(static_cast<int>(id + 100000));
        const char* badge =
            n->particle_emitter.has_value() && n->particle_emitter->enabled ? "on" : "off";
        if (selectableRow(n->name.c_str(), state.focus_emitter == id, badge)) {
            state.focus_emitter = id;
            if (ctx.select_node) {
                ctx.select_node(id);
            }
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::Separator();
    if (state.focus_system != 0) {
        drawAddEmitterButtons(scene, state.focus_system, ctx, state);
        // Refresh emitter list after potential adds
        collectEmitters(scene, state.focus_system, emitters);
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right: tabs Graph / Details
    ImGui::BeginChild("tugela_detail", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
    if (ImGui::BeginTabBar("tugela_tabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("Graph")) {
            state.tab = TugelaEditorTab::Graph;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Details")) {
            state.tab = TugelaEditorTab::Details;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (state.tab == TugelaEditorTab::Graph) {
        // Graph on top; compact module inspector under for selected node
        const float graph_h = std::max(220.0f, ImGui::GetContentRegionAvail().y * 0.55f);
        ImGui::BeginChild("graph_wrap", ImVec2(0.0f, graph_h), ImGuiChildFlags_None);
        drawModuleGraph(state, scene, emitters, ctx);
        ImGui::EndChild();

        ImGui::Spacing();
        Node* emitter = scene.find(state.focus_emitter);
        if (emitter && emitter->particle_emitter.has_value()) {
            ImGui::BeginChild("graph_module_insp", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
            ImGui::TextColored(ImVec4(0.90f, 0.92f, 0.94f, 1.0f), "%s", emitter->name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(kMuted, " / %s", moduleLabel(state.focus_module));
            ImGui::Spacing();
            drawModuleInspector(*emitter->particle_emitter, state.focus_module, ctx);
            ImGui::EndChild();
        } else {
            ImGui::TextDisabled("Select a module node to edit parameters.");
        }
    } else {
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
    }
    ImGui::EndChild();

    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::End();
    ImGui::PopStyleColor(3);
}

} // namespace genesis
