#include "editor/TugelaEditor.hpp"

#include "editor/EditorTheme.hpp"
#include "editor/OscProtocolUi.hpp"
#include "particles/ParticleCurve.hpp"
#include "protocol/OscParticleEmit.hpp"
#include "scene/ScenePath.hpp"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

#include <imgui.h>

namespace genesis {

namespace {

constexpr ImVec4 kAccent{0.34f, 0.78f, 0.82f, 1.0f};
constexpr ImVec4 kMuted{0.50f, 0.54f, 0.58f, 1.0f};
constexpr ImVec4 kPanel{0.032f, 0.034f, 0.038f, 1.0f};

// Layout derived from the live font size so UI scale 2+ stays readable
// (hardcoded px crushed labels/rows when fonts grew).
struct TugelaMetrics {
    float font = 16.0f;
    float stage_w = 180.0f;
    float stage_gap_x = 24.0f;
    float stage_header_h = 28.0f;
    float stack_row_h = 24.0f;
    float stage_pad = 8.0f;
    float emitter_lane_gap = 40.0f;
    float systems_w = 180.0f;
    float emitters_w = 200.0f;
    float header_h = 40.0f;
    float checkbox = 14.0f;
    float timing_h = 120.0f;
    float curve_plot_h = 64.0f;
    float color_bar_h = 22.0f;
    float pad = 10.0f;
};

TugelaMetrics makeMetrics() {
    const float f = std::max(ImGui::GetFontSize(), 12.0f);
    const ImGuiStyle& st = ImGui::GetStyle();
    TugelaMetrics m;
    m.font = f;
    m.pad = st.FramePadding.x;
    // Fit longest stack labels ("Sprite Renderer", "Mesh Renderer", …) + checkbox.
    const float label_w = std::max(ImGui::CalcTextSize("Sprite Renderer").x,
                                   ImGui::CalcTextSize("Mesh Renderer").x);
    m.checkbox = std::round(f * 0.85f);
    m.stage_w = std::ceil(label_w + m.checkbox + m.pad * 4.0f + 12.0f);
    m.stage_gap_x = std::max(18.0f, f * 1.1f);
    m.stage_header_h = std::ceil(f + st.FramePadding.y * 2.0f + 4.0f);
    m.stack_row_h = std::ceil(f + st.FramePadding.y * 2.0f + 6.0f);
    m.stage_pad = std::max(6.0f, f * 0.35f);
    m.emitter_lane_gap = std::max(28.0f, f * 1.6f);
    m.systems_w = std::max(160.0f, f * 9.0f);
    m.emitters_w = std::max(180.0f, f * 10.5f);
    m.header_h = std::ceil(f + st.FramePadding.y * 2.0f + 12.0f);
    m.timing_h = std::ceil(f * 2.0f + st.FramePadding.y * 6.0f + st.ItemSpacing.y * 4.0f + 36.0f);
    m.curve_plot_h = std::max(48.0f, f * 2.4f);
    m.color_bar_h = std::max(16.0f, f * 0.9f);
    return m;
}

TugelaMetrics scaleGraphMetrics(TugelaMetrics m, float zoom) {
    const float z = std::clamp(zoom, 0.35f, 2.5f);
    m.font *= z;
    m.stage_w *= z;
    m.stage_gap_x *= z;
    m.stage_header_h *= z;
    m.stack_row_h *= z;
    m.stage_pad *= z;
    m.emitter_lane_gap *= z;
    m.checkbox *= z;
    m.pad *= z;
    return m;
}

void graphAddText(ImDrawList* draw, const ImVec2& pos, ImU32 col, const char* text, float font_px) {
    draw->AddText(ImGui::GetFont(), font_px, pos, col, text);
}

float graphTextWidth(const char* text, float font_px) {
    return ImGui::GetFont()->CalcTextSizeA(font_px, FLT_MAX, 0.0f, text).x;
}

const char* stageLabel(TugelaModuleKind kind) {
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
        return "Render";
    case TugelaModuleKind::Trail:
        return "Trail";
    default:
        return "Stage";
    }
}

const char* stackLabel(TugelaStackMod mod) {
    switch (mod) {
    case TugelaStackMod::SpawnRate:
        return "Spawn Rate";
    case TugelaStackMod::Burst:
        return "Burst";
    case TugelaStackMod::Shape:
        return "Shape Location";
    case TugelaStackMod::SpawnByDistance:
        return "Spawn by Distance";
    case TugelaStackMod::Lifetime:
        return "Lifetime";
    case TugelaStackMod::Velocity:
        return "Velocity";
    case TugelaStackMod::Size:
        return "Size";
    case TugelaStackMod::Color:
        return "Color";
    case TugelaStackMod::Gravity:
        return "Gravity / Lift";
    case TugelaStackMod::Drag:
        return "Drag";
    case TugelaStackMod::PointForce:
        return "Point Force";
    case TugelaStackMod::Vortex:
        return "Vortex";
    case TugelaStackMod::Noise:
        return "Noise";
    case TugelaStackMod::Accel:
        return "Acceleration";
    case TugelaStackMod::KillPlane:
        return "Kill Plane";
    case TugelaStackMod::KillBox:
        return "Kill Box";
    case TugelaStackMod::FluidCoupled:
        return "Fluid Coupled";
    case TugelaStackMod::SizeOverLife:
        return "Size over Life";
    case TugelaStackMod::ColorOverLife:
        return "Color over Life";
    case TugelaStackMod::ScaleBySpeed:
        return "Scale by Speed";
    case TugelaStackMod::Sprite:
        return "Sprite Renderer";
    case TugelaStackMod::Mesh:
        return "Mesh Renderer";
    case TugelaStackMod::Trail:
        return "Trail Renderer";
    case TugelaStackMod::OscDrive:
        return "OSC Drive";
    default:
        return "Module";
    }
}

TugelaModuleKind stageForStack(TugelaStackMod mod) {
    switch (mod) {
    case TugelaStackMod::SpawnRate:
    case TugelaStackMod::Burst:
    case TugelaStackMod::Shape:
    case TugelaStackMod::SpawnByDistance:
    case TugelaStackMod::OscDrive:
        return TugelaModuleKind::Spawn;
    case TugelaStackMod::Lifetime:
    case TugelaStackMod::Velocity:
    case TugelaStackMod::Size:
    case TugelaStackMod::Color:
        return TugelaModuleKind::Initialize;
    case TugelaStackMod::Gravity:
    case TugelaStackMod::Drag:
    case TugelaStackMod::PointForce:
    case TugelaStackMod::Vortex:
    case TugelaStackMod::Noise:
    case TugelaStackMod::Accel:
    case TugelaStackMod::KillPlane:
    case TugelaStackMod::KillBox:
    case TugelaStackMod::FluidCoupled:
        return TugelaModuleKind::Update;
    case TugelaStackMod::SizeOverLife:
    case TugelaStackMod::ColorOverLife:
    case TugelaStackMod::ScaleBySpeed:
        return TugelaModuleKind::OverLife;
    case TugelaStackMod::Sprite:
    case TugelaStackMod::Mesh:
        return TugelaModuleKind::Sprite;
    case TugelaStackMod::Trail:
        return TugelaModuleKind::Trail;
    default:
        return TugelaModuleKind::Spawn;
    }
}

std::uint32_t optionalBitFor(TugelaStackMod mod) {
    switch (mod) {
    case TugelaStackMod::Burst:
        return kStackBurst;
    case TugelaStackMod::Gravity:
        return kStackGravity;
    case TugelaStackMod::Drag:
        return kStackDrag;
    case TugelaStackMod::PointForce:
        return kStackPointForce;
    case TugelaStackMod::Vortex:
        return kStackVortex;
    case TugelaStackMod::Noise:
        return kStackNoise;
    case TugelaStackMod::Accel:
        return kStackAccel;
    case TugelaStackMod::KillPlane:
        return kStackKillPlane;
    case TugelaStackMod::ScaleBySpeed:
        return kStackScaleBySpeed;
    case TugelaStackMod::Trail:
        return kStackTrail;
    case TugelaStackMod::OscDrive:
        return kStackOscDrive;
    default:
        return 0;
    }
}

bool isOptionalStackMod(TugelaStackMod mod) {
    return optionalBitFor(mod) != 0;
}

bool stackEnabled(TugelaStackMod mod, const ParticleEmitter& e) {
    switch (mod) {
    case TugelaStackMod::SpawnRate:
        return e.mod_spawn_rate && e.rate_per_second > 0.0f;
    case TugelaStackMod::Burst:
        return particleStackHas(e.module_enabled, kStackBurst);
    case TugelaStackMod::Shape:
        return e.mod_shape;
    case TugelaStackMod::Lifetime:
        return e.mod_lifetime;
    case TugelaStackMod::Velocity:
        return e.mod_velocity;
    case TugelaStackMod::Size:
        return e.mod_size;
    case TugelaStackMod::Color:
        return e.mod_color;
    case TugelaStackMod::Gravity:
        return particleStackHas(e.module_enabled, kStackGravity);
    case TugelaStackMod::Drag:
        return particleStackHas(e.module_enabled, kStackDrag);
    case TugelaStackMod::PointForce:
        return particleStackHas(e.module_enabled, kStackPointForce);
    case TugelaStackMod::Vortex:
        return particleStackHas(e.module_enabled, kStackVortex);
    case TugelaStackMod::Noise:
        return particleStackHas(e.module_enabled, kStackNoise);
    case TugelaStackMod::Accel:
        return particleStackHas(e.module_enabled, kStackAccel);
    case TugelaStackMod::KillPlane:
        return particleStackHas(e.module_enabled, kStackKillPlane);
    case TugelaStackMod::KillBox:
        return e.kill_box;
    case TugelaStackMod::FluidCoupled:
        return e.fluid_coupled;
    case TugelaStackMod::SizeOverLife:
        return e.mod_size_over_life && e.size_over_life.count > 0;
    case TugelaStackMod::ColorOverLife:
        return e.mod_color_over_life && e.color_over_life.count > 0;
    case TugelaStackMod::ScaleBySpeed:
        return particleStackHas(e.module_enabled, kStackScaleBySpeed);
    case TugelaStackMod::Sprite:
        return e.mod_sprite;
    case TugelaStackMod::Mesh:
        return e.mod_mesh;
    case TugelaStackMod::Trail:
        return particleStackHas(e.module_enabled, kStackTrail);
    case TugelaStackMod::OscDrive:
        return particleStackHas(e.module_enabled, kStackOscDrive);
    case TugelaStackMod::SpawnByDistance:
        return e.spawn_per_unit > 0.0f;
    default:
        return false;
    }
}

// True when the module is on the emitter stack (shown even if disabled).
bool stackPresent(TugelaStackMod mod, const ParticleEmitter& e) {
    switch (mod) {
    case TugelaStackMod::SpawnRate:
    case TugelaStackMod::Shape:
    case TugelaStackMod::Lifetime:
    case TugelaStackMod::Velocity:
    case TugelaStackMod::Size:
    case TugelaStackMod::Color:
    case TugelaStackMod::SizeOverLife:
    case TugelaStackMod::ColorOverLife:
    case TugelaStackMod::Sprite:
    case TugelaStackMod::Mesh:
        return true; // always listed under Render — Mesh was the hidden Sphere fallback
    case TugelaStackMod::Burst:
    case TugelaStackMod::Gravity:
    case TugelaStackMod::Drag:
    case TugelaStackMod::PointForce:
    case TugelaStackMod::Vortex:
    case TugelaStackMod::Noise:
    case TugelaStackMod::Accel:
    case TugelaStackMod::KillPlane:
    case TugelaStackMod::ScaleBySpeed:
    case TugelaStackMod::Trail:
    case TugelaStackMod::OscDrive: {
        const std::uint32_t bit = optionalBitFor(mod);
        return bit != 0 && particleStackHas(e.module_stack, bit);
    }
    // Directory modules without stack bits yet — presence tracks the classic runtime flag.
    case TugelaStackMod::SpawnByDistance:
        return e.spawn_per_unit > 0.0f;
    case TugelaStackMod::KillBox:
        return e.kill_box;
    case TugelaStackMod::FluidCoupled:
        return e.fluid_coupled;
    default:
        return false;
    }
}

void seedOptionalDefaults(TugelaStackMod mod, ParticleEmitter& e) {
    switch (mod) {
    case TugelaStackMod::Burst:
        if (e.burst_count == 0 && e.burst_entries == 0) {
            e.burst_count = 32;
        }
        break;
    case TugelaStackMod::PointForce:
        if (std::fabs(e.point_force_strength) < 1e-6f) {
            e.point_force_strength = 4.0f;
        }
        break;
    case TugelaStackMod::Vortex:
        if (std::fabs(e.vortex_strength) < 1e-6f) {
            e.vortex_strength = 2.0f;
        }
        break;
    case TugelaStackMod::Noise:
        if (e.noise_strength < 1e-6f) {
            e.noise_strength = 0.15f;
        }
        break;
    default:
        break;
    }
}

void setStackEnabled(TugelaStackMod mod, ParticleEmitter& e, bool on) {
    switch (mod) {
    case TugelaStackMod::SpawnRate:
        e.mod_spawn_rate = on;
        if (!on) {
            e.rate_per_second = 0.0f;
        } else if (e.rate_per_second <= 0.0f) {
            e.rate_per_second = 14.0f;
        }
        break;
    case TugelaStackMod::Shape:
        e.mod_shape = on;
        break;
    case TugelaStackMod::Lifetime:
        e.mod_lifetime = on;
        break;
    case TugelaStackMod::Velocity:
        e.mod_velocity = on;
        break;
    case TugelaStackMod::Size:
        e.mod_size = on;
        break;
    case TugelaStackMod::Color:
        e.mod_color = on;
        break;
    case TugelaStackMod::KillBox:
        e.kill_box = on;
        break;
    case TugelaStackMod::FluidCoupled:
        e.fluid_coupled = on;
        break;
    case TugelaStackMod::SpawnByDistance:
        if (on) {
            if (e.spawn_per_unit <= 0.0f) {
                e.spawn_per_unit = 8.0f;
            }
        } else {
            e.spawn_per_unit = 0.0f;
        }
        break;
    case TugelaStackMod::SizeOverLife:
        e.mod_size_over_life = on;
        if (on && e.size_over_life.count == 0) {
            e.size_over_life = ParticleCurve1::linear(1.0f, e.size_end_multiplier);
        }
        break;
    case TugelaStackMod::ColorOverLife:
        e.mod_color_over_life = on;
        if (on && e.color_over_life.count == 0) {
            e.color_over_life = ParticleCurveColor::linear(e.color_start, e.color_end);
        }
        break;
    case TugelaStackMod::Sprite:
        e.mod_sprite = on;
        // Do not fall back to Sphere — that hid a Mesh path Spencer could not toggle off.
        break;
    case TugelaStackMod::Mesh:
        e.mod_mesh = on;
        if (on) {
            if (e.render_mesh != ParticleRenderMesh::Cube &&
                e.render_mesh != ParticleRenderMesh::Sphere) {
                e.render_mesh = ParticleRenderMesh::Sphere;
            }
        }
        break;
    case TugelaStackMod::Burst:
    case TugelaStackMod::Gravity:
    case TugelaStackMod::Drag:
    case TugelaStackMod::PointForce:
    case TugelaStackMod::Vortex:
    case TugelaStackMod::Noise:
    case TugelaStackMod::Accel:
    case TugelaStackMod::KillPlane:
    case TugelaStackMod::ScaleBySpeed:
    case TugelaStackMod::Trail:
    case TugelaStackMod::OscDrive: {
        const std::uint32_t bit = optionalBitFor(mod);
        if (bit == 0) {
            break;
        }
        // Add/enable keeps the leaf on the stack; disable only clears module_enabled.
        particleStackSet(e.module_stack, bit, true);
        particleStackSet(e.module_enabled, bit, on);
        if (on) {
            seedOptionalDefaults(mod, e);
        }
        syncOptionalStackFlags(e);
        break;
    }
    default:
        break;
    }
}

void removeFromStack(TugelaStackMod mod, ParticleEmitter& e) {
    const std::uint32_t bit = optionalBitFor(mod);
    if (bit == 0) {
        // Core modules: disable only (always listed).
        setStackEnabled(mod, e, false);
        return;
    }
    particleStackSet(e.module_stack, bit, false);
    particleStackSet(e.module_enabled, bit, false);
    syncOptionalStackFlags(e);
}

void collectStageStack(TugelaModuleKind stage, const ParticleEmitter& e,
                       std::vector<TugelaStackMod>& out) {
    out.clear();
    for (int i = 0; i < static_cast<int>(TugelaStackMod::Count); ++i) {
        const auto mod = static_cast<TugelaStackMod>(i);
        if (stageForStack(mod) == stage && stackPresent(mod, e)) {
            out.push_back(mod);
        }
    }
}

// Full catalog for a stage — derived from TugelaStackMod::Count so new leaf
// modules cannot be omitted from the "+" directory.
void collectStageDirectory(TugelaModuleKind stage, std::vector<TugelaStackMod>& out) {
    out.clear();
    for (int i = 0; i < static_cast<int>(TugelaStackMod::Count); ++i) {
        const auto mod = static_cast<TugelaStackMod>(i);
        if (stageForStack(mod) == stage) {
            out.push_back(mod);
        }
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
        if (const Node* parent = scene.find(*n->parent);
            parent && (parent->is_particle_system ||
                       (!parent->particle_emitter.has_value() && !parent->mesh_resource.has_value() &&
                        !parent->camera.has_value() && !parent->light.has_value()))) {
            return *n->parent;
        }
    }
    return 0;
}

void mark(ParticleEmitterUiContext& ctx, bool rebuild = false) {
    if (ctx.mark_dirty) {
        ctx.mark_dirty();
    }
    if (rebuild) {
        if (ctx.reset_particles) {
            ctx.reset_particles();
        }
    } else if (ctx.apply_particles) {
        ctx.apply_particles();
    }
}

void drawTimingStrip(ParticleEmitter& e, ParticleEmitterUiContext& ctx, const TugelaMetrics& m) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.022f, 0.024f, 0.028f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(m.pad, m.pad * 0.85f));
    // Auto height — 2×2 grid of fields; fixed 78px crushed at UI scale 2.
    ImGui::BeginChild("tugela_timings", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ImGui::TextColored(kAccent, "Timings");
    ImGui::SameLine(0.0f, m.pad);
    ImGui::TextColored(kMuted, "emitter clock");
    ImGui::Spacing();

    bool dirty = false;
    if (ImGui::BeginTable("tugela_timing_fields", 2,
                          ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        dirty |= ImGui::DragFloat("Duration", &e.duration_seconds, 0.05f, 0.0f, 600.0f, "%.2f");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        dirty |= ImGui::DragFloat("Delay", &e.start_delay_seconds, 0.01f, 0.0f, 60.0f, "%.2f");
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        dirty |= ImGui::DragFloat("Life min", &e.lifetime_min, 0.01f, 0.01f, 60.0f, "%.2f");
        e.lifetime_max = std::max(e.lifetime_max, e.lifetime_min);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        dirty |= ImGui::DragFloat("Rate / s", &e.rate_per_second, 0.5f, 0.0f, 20000.0f, "%.1f");
        ImGui::EndTable();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    if (dirty) {
        mark(ctx);
    }
}

void drawLeanSizeCurve(ParticleCurve1& curve, bool& dirty, const TugelaMetrics& m) {
    ImGui::TextColored(kMuted, "Size over life");
    float samples[32];
    const int n = 32;
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n - 1);
        samples[i] = curve.evaluate(t);
    }
    ImGui::PushStyleColor(ImGuiCol_PlotLines, kAccent);
    ImGui::PlotLines("##size_curve", samples, n, 0, nullptr, 0.0f, 2.0f,
                     ImVec2(-1.0f, m.curve_plot_h));
    ImGui::PopStyleColor();

    int keys = static_cast<int>(curve.count);
    if (ImGui::DragInt("Size keys", &keys, 1, 2, 4)) {
        curve.count = static_cast<std::uint8_t>(std::clamp(keys, 2, 4));
        dirty = true;
    }
    const float field = std::max(72.0f, m.font * 4.5f);
    for (std::uint8_t i = 0; i < curve.count; ++i) {
        ImGui::PushID(static_cast<int>(300 + i));
        ImGui::SetNextItemWidth(field);
        dirty |= ImGui::DragFloat("t", &curve.keys[i].t, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(field);
        dirty |= ImGui::DragFloat("mul", &curve.keys[i].v, 0.01f, 0.0f, 4.0f, "%.2f");
        ImGui::PopID();
    }
}

void drawLeanColorCurve(ParticleCurveColor& curve, bool& dirty, const TugelaMetrics& m) {
    ImGui::TextColored(kMuted, "Color over life");
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = m.color_bar_h;
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
    ImGui::Dummy(ImVec2(width, height + m.pad * 0.5f));

    int keys = static_cast<int>(curve.count);
    if (ImGui::DragInt("Color keys", &keys, 1, 2, 4)) {
        curve.count = static_cast<std::uint8_t>(std::clamp(keys, 2, 4));
        dirty = true;
    }
    const float field = std::max(72.0f, m.font * 4.5f);
    for (std::uint8_t i = 0; i < curve.count; ++i) {
        ImGui::PushID(static_cast<int>(400 + i));
        ImGui::SetNextItemWidth(field);
        dirty |= ImGui::DragFloat("t##c", &curve.keys[i].t, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::SameLine();
        dirty |= ImGui::ColorEdit4("rgba", &curve.keys[i].v.x,
                                   ImGuiColorEditFlags_AlphaPreviewHalf |
                                       ImGuiColorEditFlags_NoInputs);
        ImGui::PopID();
    }
}

void drawCurvePanel(ParticleEmitter& e, ParticleEmitterUiContext& ctx, const TugelaMetrics& m) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanel);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::BeginChild("tugela_curves", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ImGui::TextColored(kAccent, "Curves");
    ImGui::SameLine(0.0f, m.pad);
    ImGui::TextColored(kMuted, "lean over-life graphs");
    ImGui::Spacing();

    bool dirty = false;
    if (ImGui::Button("Rebuild from endpoints")) {
        rebuildEmitterCurvesFromEndpoints(e);
        dirty = true;
    }
    ImGui::Spacing();
    drawLeanSizeCurve(e.size_over_life, dirty, m);
    ImGui::Spacing();
    drawLeanColorCurve(e.color_over_life, dirty, m);

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    if (dirty) {
        syncEmitterCurvesFromEndpoints(e);
        mark(ctx);
    }
}

bool selectableRow(const char* label, bool selected, const char* badge) {
    const ImVec2 row_min = ImGui::GetCursorScreenPos();
    const float row_w = ImGui::GetContentRegionAvail().x;
    const bool clicked = ImGui::Selectable(label, selected, ImGuiSelectableFlags_AllowDoubleClick,
                                           ImVec2(row_w, 0.0f));
    if (badge && badge[0] != '\0') {
        const ImVec2 badge_sz = ImGui::CalcTextSize(badge);
        const float x = row_min.x + row_w - badge_sz.x - ImGui::GetStyle().FramePadding.x;
        const float y = row_min.y + (ImGui::GetTextLineHeight() - badge_sz.y) * 0.5f;
        ImGui::GetWindowDrawList()->AddText(ImVec2(x, y),
                                            ImGui::GetColorU32(ImVec4(kMuted.x, kMuted.y, kMuted.z, 1.0f)),
                                            badge);
    }
    return clicked;
}

void drawStackModuleInspector(ParticleEmitter& e, TugelaStackMod mod, ParticleEmitterUiContext& ctx,
                              const std::string& node_path, TugelaEditorState* state) {
    bool dirty = false;
    bool removed = false;
    ImGui::TextColored(kAccent, "%s", stackLabel(mod));
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::TextColored(kMuted, "%s", stackEnabled(mod, e) ? "enabled" : "disabled");
    ImGui::Spacing();
    {
        bool en = stackEnabled(mod, e);
        if (ImGui::Checkbox("Enabled on emitter", &en)) {
            setStackEnabled(mod, e, en);
            dirty = true;
            if (mod == TugelaStackMod::OscDrive && ctx.osc && ctx.osc->protocol) {
                OscParticleEmitSettings emit = ctx.osc->protocol->particleEmitSettings();
                if (en) {
                    emit.enabled = true;
                    emit.track_emitter_position = true;
                    emit.emitter_path = node_path;
                    ctx.osc->protocol->setParticleEmitSettings(emit);
                } else if (emit.emitter_path == node_path) {
                    emit.enabled = false;
                    ctx.osc->protocol->setParticleEmitSettings(emit);
                }
            }
        }
        if (isOptionalStackMod(mod) && stackPresent(mod, e)) {
            ImGui::SameLine(0.0f, 12.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.10f, 0.10f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.40f, 0.14f, 0.14f, 1.0f));
            if (ImGui::SmallButton("Remove")) {
                removeFromStack(mod, e);
                removed = true;
                dirty = true;
                if (mod == TugelaStackMod::OscDrive && ctx.osc && ctx.osc->protocol) {
                    OscParticleEmitSettings emit = ctx.osc->protocol->particleEmitSettings();
                    if (emit.emitter_path == node_path) {
                        emit.enabled = false;
                        ctx.osc->protocol->setParticleEmitSettings(emit);
                    }
                }
            }
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Remove from stack (disable keeps the module listed)");
            }
        }
    }
    ImGui::Spacing();
    if (removed) {
        if (state) {
            // Fall back to a stage core leaf so the inspector isn't orphaned.
            state->focus_stack = TugelaStackMod::SpawnRate;
            state->focus_module = TugelaModuleKind::Spawn;
        }
        syncEmitterCurvesFromEndpoints(e);
        mark(ctx);
        return;
    }

    switch (mod) {
    case TugelaStackMod::SpawnRate:
        dirty |= ImGui::DragFloat("Rate / s", &e.rate_per_second, 0.5f, 0.0f, 20000.0f);
        if (ctx.osc) {
            drawOscLinkButton(*ctx.osc, OscParamTarget::ParticleRate, node_path, 0, "tg_rate");
        }
        break;
    case TugelaStackMod::SpawnByDistance:
        dirty |= ImGui::DragFloat("Spawn / meter", &e.spawn_per_unit, 0.1f, 0.0f, 500.0f);
        ImGui::TextDisabled("Emits as the emitter travels (OSC trackers, movers).");
        break;
    case TugelaStackMod::Burst: {
        int burst = static_cast<int>(e.burst_count);
        if (ImGui::DragInt("Burst @ t=0", &burst, 1, 0, 100000)) {
            e.burst_count = static_cast<std::uint32_t>(std::max(burst, 0));
            dirty = true;
        }
        int burst_entries = static_cast<int>(e.burst_entries);
        if (ImGui::DragInt("Timed bursts", &burst_entries, 1, 0, 4)) {
            e.burst_entries = static_cast<std::uint8_t>(std::clamp(burst_entries, 0, 4));
            dirty = true;
        }
        for (std::uint8_t bi = 0; bi < e.burst_entries; ++bi) {
            ImGui::PushID(static_cast<int>(bi));
            dirty |= ImGui::DragFloat("Burst time", &e.bursts[bi].time_seconds, 0.01f, 0.0f, 120.0f);
            int bc = static_cast<int>(e.bursts[bi].count);
            if (ImGui::DragInt("Burst count", &bc, 1, 0, 100000)) {
                e.bursts[bi].count = static_cast<std::uint32_t>(std::max(bc, 0));
                dirty = true;
            }
            ImGui::PopID();
        }
        break;
    }
    case TugelaStackMod::Shape: {
        const char* shapes[] = {"Point", "Sphere", "Cone", "Box", "Ring (cylinder)"};
        int shape = static_cast<int>(e.spawn_shape);
        if (ImGui::Combo("Spawn shape", &shape, shapes, IM_ARRAYSIZE(shapes))) {
            e.spawn_shape = static_cast<ParticleSpawnShape>(shape);
            dirty = true;
        }
        dirty |= ImGui::DragFloat("Spawn radius", &e.spawn_radius, 0.01f, 0.0f, 50.0f);
        dirty |= ImGui::DragFloat("Spawn height", &e.spawn_box_height, 0.01f, 0.0f, 50.0f);
        dirty |= ImGui::DragFloat("Cone angle", &e.cone_angle_degrees, 0.5f, 0.0f, 89.0f);
        break;
    }
    case TugelaStackMod::Lifetime:
        dirty |= ImGui::DragFloatRange2("Lifetime", &e.lifetime_min, &e.lifetime_max, 0.01f, 0.01f,
                                        60.0f);
        break;
    case TugelaStackMod::Velocity: {
        dirty |= ImGui::DragFloatRange2("Speed", &e.speed_min, &e.speed_max, 0.01f, 0.0f, 100.0f);
        const char* vel_modes[] = {"Cone", "From point", "Random", "Horizontal plane"};
        int vel_mode = static_cast<int>(e.velocity_mode);
        if (ImGui::Combo("Velocity mode", &vel_mode, vel_modes, IM_ARRAYSIZE(vel_modes))) {
            e.velocity_mode = static_cast<ParticleVelocityMode>(vel_mode);
            dirty = true;
        }
        dirty |= ImGui::DragFloat("Inherit velocity", &e.inherit_velocity, 0.01f, 0.0f, 1.0f);
        break;
    }
    case TugelaStackMod::Size:
        dirty |= ImGui::DragFloatRange2("Size", &e.size_min, &e.size_max, 0.001f, 0.02f, 10.0f);
        dirty |= ImGui::DragFloat("Size end mul", &e.size_end_multiplier, 0.01f, 0.0f, 2.0f);
        break;
    case TugelaStackMod::Color:
        dirty |= ImGui::ColorEdit4("Color start", &e.color_start.x,
                                   ImGuiColorEditFlags_AlphaPreviewHalf);
        dirty |=
            ImGui::ColorEdit4("Color end", &e.color_end.x, ImGuiColorEditFlags_AlphaPreviewHalf);
        break;
    case TugelaStackMod::Gravity:
        dirty |= ImGui::DragFloat3("Gravity (+Y = lift)", &e.gravity.x, 0.02f);
        break;
    case TugelaStackMod::Drag:
        dirty |= ImGui::DragFloat("Drag amount", &e.drag, 0.01f, 0.0f, 10.0f);
        break;
    case TugelaStackMod::PointForce:
        dirty |= ImGui::DragFloat3("Point position", &e.point_force_position.x, 0.05f);
        dirty |= ImGui::DragFloat("Strength", &e.point_force_strength, 0.1f, -200.0f, 200.0f);
        break;
    case TugelaStackMod::Vortex:
        dirty |= ImGui::DragFloat("Vortex strength", &e.vortex_strength, 0.05f, -50.0f, 50.0f);
        dirty |= ImGui::DragFloat3("Vortex axis", &e.vortex_axis.x, 0.05f);
        dirty |= ImGui::DragFloat("Vortex radius", &e.vortex_radius, 0.05f, 0.1f, 100.0f);
        break;
    case TugelaStackMod::Noise:
        dirty |= ImGui::DragFloat("Noise strength", &e.noise_strength, 0.005f, 0.0f, 5.0f);
        dirty |= ImGui::DragFloat("Noise frequency", &e.noise_frequency, 0.01f, 0.01f, 20.0f);
        break;
    case TugelaStackMod::Accel:
        dirty |= ImGui::DragFloat3("Acceleration", &e.acceleration.x, 0.05f);
        break;
    case TugelaStackMod::KillPlane:
        dirty |= ImGui::DragFloat("Kill plane Y", &e.kill_plane_y, 0.05f);
        break;
    case TugelaStackMod::KillBox:
        dirty |= ImGui::DragFloat3("Kill box min", &e.kill_box_min.x, 0.05f);
        dirty |= ImGui::DragFloat3("Kill box max", &e.kill_box_max.x, 0.05f);
        ImGui::TextDisabled("Particles die outside this world AABB.");
        break;
    case TugelaStackMod::FluidCoupled:
        ImGui::TextWrapped(
            "Couples particle motion to the scene fluid field when one is present.");
        break;
    case TugelaStackMod::SizeOverLife:
        dirty |= ImGui::DragFloat("Size end mul", &e.size_end_multiplier, 0.01f, 0.0f, 2.0f);
        if (ImGui::Button("Rebuild size curve")) {
            e.size_over_life = ParticleCurve1::linear(1.0f, e.size_end_multiplier);
            dirty = true;
        }
        break;
    case TugelaStackMod::ColorOverLife:
        if (ImGui::Button("Rebuild color curve")) {
            e.color_over_life = ParticleCurveColor::linear(e.color_start, e.color_end);
            dirty = true;
        }
        break;
    case TugelaStackMod::ScaleBySpeed:
        dirty |= ImGui::DragFloatRange2("Speed range", &e.scale_by_speed_range_min,
                                        &e.scale_by_speed_range_max, 0.01f, 0.0f, 50.0f);
        dirty |= ImGui::DragFloatRange2("Size mul", &e.scale_by_speed_mul_min,
                                        &e.scale_by_speed_mul_max, 0.01f, 0.0f, 8.0f);
        break;
    case TugelaStackMod::Sprite:
        dirty |= ImGui::Checkbox("Unlit", &e.unlit);
        dirty |= ImGui::Checkbox("Soft additive", &e.additive);
        dirty |= ImGui::DragFloat("Sprite aspect", &e.sprite_aspect, 0.01f, 0.05f, 8.0f);
        dirty |= ImGui::DragFloat("Velocity stretch", &e.velocity_stretch, 0.01f, 0.0f, 8.0f);
        ImGui::TextDisabled(
            "Soft circle · stretch along velocity · Sprite+Mesh+Trail off = invisible");
        break;
    case TugelaStackMod::Mesh: {
        dirty |= ImGui::Checkbox("Unlit", &e.unlit);
        dirty |= ImGui::Checkbox("Soft additive", &e.additive);
        const char* meshes[] = {"Cube", "Sphere"};
        int mesh = (e.render_mesh == ParticleRenderMesh::Cube) ? 0 : 1;
        if (ImGui::Combo("Mesh", &mesh, meshes, IM_ARRAYSIZE(meshes))) {
            e.render_mesh =
                (mesh == 0) ? ParticleRenderMesh::Cube : ParticleRenderMesh::Sphere;
            dirty = true;
        }
        if (e.mod_sprite) {
            ImGui::TextDisabled("Sprite Renderer is on — it draws the head instead of Mesh.");
        } else {
            ImGui::TextDisabled("3D Cube / Sphere head · same batch path as lit/unlit meshes");
        }
        break;
    }
    case TugelaStackMod::Trail: {
        int segs = static_cast<int>(e.trail_segments);
        if (ImGui::DragInt("Segments", &segs, 1, 2, 24)) {
            e.trail_segments = static_cast<std::uint8_t>(std::clamp(segs, 2, 24));
            dirty = true;
        }
        dirty |= ImGui::DragFloat("Sample distance", &e.trail_sample_distance, 0.001f, 0.01f, 0.5f);
        dirty |= ImGui::DragFloat("Width (head)", &e.trail_width, 0.001f, 0.01f, 1.0f);
        dirty |= ImGui::DragFloat("Width (tip)", &e.trail_width_end, 0.001f, 0.0f, 1.0f);
        dirty |= ImGui::DragFloat("Trail alpha", &e.trail_alpha, 0.01f, 0.0f, 1.0f);
        dirty |= ImGui::DragFloat("Trail lifetime feel", &e.trail_lifetime, 0.01f, 0.05f, 8.0f);
        ImGui::TextDisabled(
            "Live: width / alpha / segments / sample / lifetime · disable stops draw · "
            "length ≈ segments × sample (lifetime can shorten).");
        break;
    }
    case TugelaStackMod::OscDrive: {
        ImGui::TextWrapped(
            "Drive this emitter from Protocol OSC — not a special world actor. "
            "XY float messages move the emitter; Link buttons bind any learned address "
            "to rate/size/etc. (same as the Protocol window binding list).");
        ImGui::Spacing();
        if (ctx.osc && ctx.osc->protocol) {
            OscParticleEmitSettings emit = ctx.osc->protocol->particleEmitSettings();
            bool emit_dirty = false;
            const bool targeting_this = emit.emitter_path == node_path;
            ImGui::TextDisabled("Path: %s", node_path.c_str());
            if (!targeting_this) {
                ImGui::TextColored(kMuted, "Protocol XY currently points at another emitter.");
                if (ImGui::Button("Point XY drive here")) {
                    emit.emitter_path = node_path;
                    emit.enabled = true;
                    emit.track_emitter_position = true;
                    emit_dirty = true;
                }
            } else {
                ImGui::TextColored(kAccent, "Protocol XY drive targets this emitter");
            }
            emit_dirty |= ImGui::Checkbox("Enable XY drive", &emit.enabled);
            emit_dirty |= ImGui::Checkbox("Track position", &emit.track_emitter_position);
            emit_dirty |= ImGui::Checkbox("Burst on message", &emit.burst_on_message);
            if (emit_dirty) {
                if (emit.enabled) {
                    emit.emitter_path = node_path;
                }
                ctx.osc->protocol->setParticleEmitSettings(emit);
            }
        } else {
            ImGui::TextDisabled("Open View → Protocol to listen / learn OSC.");
        }
        ImGui::Spacing();
        ImGui::TextColored(kMuted, "Parameter links (use Protocol Learn → Link, then click):");
        dirty |= ImGui::DragFloat("Rate / s", &e.rate_per_second, 0.5f, 0.0f, 20000.0f);
        if (ctx.osc) {
            drawOscLinkButton(*ctx.osc, OscParamTarget::ParticleRate, node_path, 0, "osc_rate");
        }
        dirty |= ImGui::DragFloatRange2("Size", &e.size_min, &e.size_max, 0.001f, 0.02f, 10.0f);
        if (ctx.osc) {
            drawOscLinkButton(*ctx.osc, OscParamTarget::ParticleSizeMin, node_path, 0, "osc_sz0");
            drawOscLinkButton(*ctx.osc, OscParamTarget::ParticleSizeMax, node_path, 0, "osc_sz1");
        }
        break;
    }
    default:
        break;
    }

    if (dirty) {
        syncEmitterCurvesFromEndpoints(e);
        mark(ctx);
    }
}

bool pointInRect(ImVec2 p, ImVec2 a, ImVec2 b) {
    return p.x >= a.x && p.x <= b.x && p.y >= a.y && p.y <= b.y;
}

float stageHeightFor(const std::vector<TugelaStackMod>& stack, const TugelaMetrics& m) {
    const int rows = std::max(1, static_cast<int>(stack.size()));
    return m.stage_header_h + m.stage_pad * 2.0f + static_cast<float>(rows) * m.stack_row_h +
           m.font * 0.7f;
}

bool drawEmitterGraphRow(TugelaEditorState& state, Node& emitter_node, int row_index,
                         ParticleEmitterUiContext& ctx, ImDrawList* draw, ImVec2 canvas_origin,
                         float& out_row_h, bool allow_pick, const TugelaMetrics& m) {
    ParticleEmitter& e = *emitter_node.particle_emitter;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    bool picked = false;

    float max_h = m.font * 4.0f;
    std::vector<TugelaStackMod> stacks[static_cast<int>(TugelaModuleKind::Count)];
    for (int si = 0; si < static_cast<int>(TugelaModuleKind::Count); ++si) {
        collectStageStack(static_cast<TugelaModuleKind>(si), e, stacks[si]);
        max_h = std::max(max_h, stageHeightFor(stacks[si], m));
    }
    out_row_h = max_h + m.emitter_lane_gap;

    const float name_gap = m.font + 4.0f;
    const float y = canvas_origin.y + state.graph_pan_y + name_gap;
    (void)row_index;
    const float x0 = canvas_origin.x + state.graph_pan_x;

    graphAddText(draw, ImVec2(x0, y - name_gap), IM_COL32(140, 150, 160, 255),
                 emitter_node.name.c_str(), m.font);
    if (!e.enabled) {
        graphAddText(
            draw,
            ImVec2(x0 + graphTextWidth(emitter_node.name.c_str(), m.font) + m.pad, y - name_gap),
            IM_COL32(120, 90, 90, 220), "off", m.font);
    }

    ImVec2 prev_center{};
    bool has_prev = false;
    TugelaStackMod popup_add_stage_mod = TugelaStackMod::Gravity;
    TugelaModuleKind popup_stage = TugelaModuleKind::Update;
    bool open_add_popup = false;

    const float add_btn = std::max(18.0f, m.font * 0.95f);
    const float check = m.checkbox;

    for (int si = 0; si < static_cast<int>(TugelaModuleKind::Count); ++si) {
        const auto stage = static_cast<TugelaModuleKind>(si);
        const std::vector<TugelaStackMod>& stack = stacks[si];
        const float x = x0 + static_cast<float>(si) * (m.stage_w + m.stage_gap_x);
        const float h = stageHeightFor(stack, m);
        const ImVec2 p0(x, y);
        const ImVec2 p1(x + m.stage_w, y + h);
        const bool stage_selected =
            state.focus_emitter == emitter_node.id && state.focus_module == stage;
        const bool stage_hovered = pointInRect(mouse, p0, p1);

        draw->AddRectFilled(p0, p1, IM_COL32(22, 26, 30, 255), 6.0f);
        draw->AddRect(p0, p1,
                      stage_selected ? IM_COL32(100, 210, 220, 255)
                                     : (stage_hovered ? IM_COL32(80, 130, 140, 220)
                                                      : IM_COL32(55, 62, 70, 220)),
                      6.0f, 0, stage_selected ? 2.0f : 1.0f);
        draw->AddRectFilled(p0, ImVec2(p1.x, p0.y + m.stage_header_h), IM_COL32(28, 36, 42, 255),
                            6.0f, ImDrawFlags_RoundCornersTop);
        const float header_text_y =
            p0.y + (m.stage_header_h - m.font) * 0.5f;
        graphAddText(draw, ImVec2(p0.x + m.pad, header_text_y), IM_COL32(220, 230, 235, 255),
                     stageLabel(stage), m.font);

        // "+ Add" opens the full stage module directory (core + optional).
        std::vector<TugelaStackMod> stage_dir;
        collectStageDirectory(stage, stage_dir);
        if (!stage_dir.empty()) {
            const float add_pad = (m.stage_header_h - add_btn) * 0.5f;
            const ImVec2 add_p0(p1.x - add_btn - m.pad * 0.6f, p0.y + add_pad);
            const ImVec2 add_p1(p1.x - m.pad * 0.6f, p0.y + add_pad + add_btn);
            const bool add_hov = pointInRect(mouse, add_p0, add_p1);
            draw->AddRectFilled(add_p0, add_p1,
                                add_hov ? IM_COL32(50, 90, 100, 255) : IM_COL32(36, 48, 54, 255),
                                3.0f);
            const float plus_w = graphTextWidth("+", m.font);
            graphAddText(draw,
                         ImVec2(add_p0.x + (add_btn - plus_w) * 0.5f,
                                add_p0.y + (add_btn - m.font) * 0.5f),
                         IM_COL32(180, 220, 230, 255), "+", m.font);
            if (add_hov) {
                ImGui::SetTooltip("Module directory · %s", stageLabel(stage));
            }
            if (allow_pick && add_hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                popup_stage = stage;
                open_add_popup = true;
                state.focus_emitter = emitter_node.id;
                state.focus_module = stage;
                picked = true;
            }
        }

        float row_y = p0.y + m.stage_header_h + m.stage_pad * 0.5f;
        for (const TugelaStackMod mod : stack) {
            const ImVec2 r0(p0.x + m.pad * 0.6f, row_y);
            const ImVec2 r1(p1.x - m.pad * 0.6f, row_y + m.stack_row_h - 2.0f);
            const bool en = stackEnabled(mod, e);
            const bool sel = state.focus_emitter == emitter_node.id && state.focus_stack == mod;
            const bool hov = pointInRect(mouse, r0, r1);

            draw->AddRectFilled(r0, r1,
                                sel ? IM_COL32(32, 70, 78, 255)
                                    : (en ? IM_COL32(30, 42, 48, 255) : IM_COL32(26, 28, 32, 255)),
                                3.0f);
            if (sel || hov) {
                draw->AddRect(r0, r1, IM_COL32(90, 180, 190, 220), 3.0f);
            }

            // Enable checkbox square — centered vertically in the row
            const float check_y = r0.y + (r1.y - r0.y - check) * 0.5f;
            const ImVec2 c0(r0.x + 4.0f, check_y);
            const ImVec2 c1(c0.x + check, c0.y + check);
            draw->AddRectFilled(c0, c1,
                                en ? IM_COL32(70, 170, 180, 255) : IM_COL32(40, 44, 48, 255), 2.0f);
            draw->AddRect(c0, c1, IM_COL32(120, 140, 150, 220), 2.0f);
            if (en) {
                const float s = check / 12.0f;
                draw->AddLine(ImVec2(c0.x + 2.5f * s, c0.y + 6.0f * s),
                              ImVec2(c0.x + 5.0f * s, c0.y + 9.0f * s), IM_COL32(10, 20, 24, 255),
                              1.6f * s);
                draw->AddLine(ImVec2(c0.x + 5.0f * s, c0.y + 9.0f * s),
                              ImVec2(c0.x + 10.0f * s, c0.y + 3.0f * s), IM_COL32(10, 20, 24, 255),
                              1.6f * s);
            }

            const float label_y = r0.y + (r1.y - r0.y - m.font) * 0.5f;
            graphAddText(draw, ImVec2(c1.x + m.pad * 0.6f, label_y),
                         en ? IM_COL32(220, 230, 235, 255) : IM_COL32(110, 118, 125, 255),
                         stackLabel(mod), m.font);

            if (hov) {
                ImGui::SetTooltip("%s · click select · click □ enable/disable", stackLabel(mod));
            }
            if (allow_pick && hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                state.focus_emitter = emitter_node.id;
                state.focus_module = stage;
                state.focus_stack = mod;
                if (ctx.select_node) {
                    ctx.select_node(emitter_node.id);
                }
                // Toggle when clicking the checkbox region
                if (pointInRect(mouse, ImVec2(c0.x - 2.0f, c0.y - 2.0f),
                                ImVec2(c1.x + 2.0f, c1.y + 2.0f))) {
                    setStackEnabled(mod, e, !en);
                    mark(ctx); // live-apply trail / forces without full restart
                }
                picked = true;
            }
            row_y += m.stack_row_h;
        }

        if (stack.empty()) {
            graphAddText(draw, ImVec2(p0.x + m.pad, p0.y + m.stage_header_h + m.stage_pad),
                         IM_COL32(100, 110, 120, 200), "empty · + to add", m.font);
        }

        const ImVec2 center((p0.x + p1.x) * 0.5f, p0.y + h * 0.5f);
        if (has_prev) {
            const ImVec2 a(prev_center.x + m.stage_w * 0.5f, prev_center.y);
            const ImVec2 b(center.x - m.stage_w * 0.5f, center.y);
            const float bend = m.stage_gap_x * 0.55f;
            draw->AddBezierCubic(a, ImVec2(a.x + bend, a.y), ImVec2(b.x - bend, b.y), b,
                                 IM_COL32(70, 150, 160, 180), 2.0f);
        }
        prev_center = center;
        has_prev = true;
    }

    if (open_add_popup) {
        ImGui::OpenPopup("tugela_add_mod");
    }
    if (ImGui::BeginPopup("tugela_add_mod")) {
        ImGui::TextColored(kAccent, "%s modules", stageLabel(popup_stage));
        ImGui::TextDisabled("Click to add · grey = already on stack");
        ImGui::Separator();
        std::vector<TugelaStackMod> directory;
        collectStageDirectory(popup_stage, directory);
        int added = 0;
        for (const TugelaStackMod mod : directory) {
            const bool on_stack = stackPresent(mod, e);
            if (on_stack) {
                ImGui::BeginDisabled();
                ImGui::MenuItem(stackLabel(mod), nullptr, true);
                ImGui::EndDisabled();
            } else if (ImGui::MenuItem(stackLabel(mod))) {
                setStackEnabled(mod, e, true);
                state.focus_stack = mod;
                state.focus_module = popup_stage;
                mark(ctx); // apply enable to living particles (esp. Trail)
                ++added;
            }
        }
        if (directory.empty()) {
            ImGui::TextDisabled("No modules for this stage.");
        } else {
            bool any_addable = false;
            for (const TugelaStackMod mod : directory) {
                if (!stackPresent(mod, e)) {
                    any_addable = true;
                    break;
                }
            }
            if (!any_addable && added == 0) {
                ImGui::Spacing();
                ImGui::TextDisabled("All modules already on stack.");
            }
        }
        ImGui::EndPopup();
        (void)popup_add_stage_mod;
    }
    return picked;
}

void drawModuleGraph(TugelaEditorState& state, Scene& scene, const std::vector<NodeId>& emitters,
                     ParticleEmitterUiContext& ctx, const TugelaMetrics& m) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.018f, 0.019f, 0.022f, 1.0f));
    ImGui::BeginChild("tugela_graph_canvas", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);

    ImGui::TextColored(kAccent, "Module graph");
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kMuted,
                       "Stack · □ enable · + directory · scroll zoom · MMB / LMB-drag pan");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    const ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    if (canvas_size.y < m.font * 8.0f) {
        canvas_size.y = m.font * 8.0f;
    }

    ImGui::InvisibleButton("##graph_bg", canvas_size);
    const bool canvas_hovered = ImGui::IsItemHovered();
    const bool canvas_active = ImGui::IsItemActive();

    // Scroll-wheel zoom toward cursor (same wheel that dollies the 3D viewport elsewhere).
    if (canvas_hovered) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (std::abs(wheel) > 0.0f) {
            const float old_zoom = std::clamp(state.graph_zoom, 0.35f, 2.5f);
            const float new_zoom =
                std::clamp(old_zoom * std::exp(wheel * 0.12f), 0.35f, 2.5f);
            if (std::abs(new_zoom - old_zoom) > 1e-5f) {
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                const float mx = mouse.x - canvas_p0.x;
                const float my = mouse.y - canvas_p0.y;
                const float ratio = new_zoom / old_zoom;
                state.graph_pan_x = mx - (mx - state.graph_pan_x) * ratio;
                state.graph_pan_y = my - (my - state.graph_pan_y) * ratio;
                state.graph_zoom = new_zoom;
            }
        }
    }

    const TugelaMetrics gm = scaleGraphMetrics(m, state.graph_zoom);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(canvas_p0, ImVec2(canvas_p0.x + canvas_size.x, canvas_p0.y + canvas_size.y),
                        IM_COL32(14, 15, 17, 255), 4.0f);

    const float grid_step = std::max(28.0f, gm.font * 1.6f);
    for (float gx = std::fmod(state.graph_pan_x, grid_step); gx < canvas_size.x; gx += grid_step) {
        draw->AddLine(ImVec2(canvas_p0.x + gx, canvas_p0.y),
                      ImVec2(canvas_p0.x + gx, canvas_p0.y + canvas_size.y),
                      IM_COL32(30, 34, 38, 120));
    }
    for (float gy = std::fmod(state.graph_pan_y, grid_step); gy < canvas_size.y; gy += grid_step) {
        draw->AddLine(ImVec2(canvas_p0.x, canvas_p0.y + gy),
                      ImVec2(canvas_p0.x + canvas_size.x, canvas_p0.y + gy),
                      IM_COL32(30, 34, 38, 120));
    }

    draw->PushClipRect(canvas_p0, ImVec2(canvas_p0.x + canvas_size.x, canvas_p0.y + canvas_size.y),
                       true);

    if (emitters.empty()) {
        graphAddText(draw, ImVec2(canvas_p0.x + gm.pad * 2.0f, canvas_p0.y + gm.pad * 2.0f),
                     IM_COL32(120, 130, 140, 255),
                     "Add emitters to this System to build the graph.", gm.font);
    }

    bool picked_node = false;
    float lane_y = state.graph_pan_y;
    int row = 0;
    for (const NodeId id : emitters) {
        Node* n = scene.find(id);
        if (!n || !n->particle_emitter.has_value()) {
            continue;
        }
        // Temporarily set pan_y so draw uses absolute lane position
        const float saved_pan_y = state.graph_pan_y;
        state.graph_pan_y = lane_y;
        float row_h = 0.0f;
        ImGui::PushID(static_cast<int>(id));
        picked_node |= drawEmitterGraphRow(state, *n, row, ctx, draw, canvas_p0, row_h,
                                           canvas_hovered, gm);
        ImGui::PopID();
        state.graph_pan_y = saved_pan_y;
        lane_y += row_h;
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
    auto add = [&](const char* name, const ParticleEmitter& preset) {
        system->is_particle_system = true;
        const NodeId id = scene.createParticleEmitterNode(name, system_id, preset);
        mark(ctx, true); // rebuild so new emitter clocks + particles sync
        state.focus_emitter = id;
        state.focus_system = system_id;
        if (ctx.select_node) {
            // Hierarchy shows Systems only — keep selection on the System actor.
            ctx.select_node(system_id);
        }
    };
    // Full-width stacked buttons — side-by-side thirds crush labels at UI scale 2.
    if (ImGui::Button("Soft Circles", ImVec2(-1.0f, 0))) {
        add("Soft Circles", makeGentleCirclesEmitter());
    }
    if (ImGui::Button("Trail", ImVec2(-1.0f, 0))) {
        ParticleEmitter trail = makeGentleSoftEmitter();
        trail.rate_per_second = 6.0f;
        particleStackSet(trail.module_stack, kStackTrail, true);
        particleStackSet(trail.module_enabled, kStackTrail, true);
        particleStackSet(trail.module_stack, kStackGravity, true);
        particleStackSet(trail.module_enabled, kStackGravity, true);
        syncOptionalStackFlags(trail);
        add("Trail", trail);
    }
    if (ImGui::Button("Circles + Trail", ImVec2(-1.0f, 0))) {
        add("Soft Circles", makeGentleCirclesEmitter());
        ParticleEmitter trail = makeGentleSoftEmitter();
        trail.rate_per_second = 5.0f;
        particleStackSet(trail.module_stack, kStackTrail, true);
        particleStackSet(trail.module_enabled, kStackTrail, true);
        syncOptionalStackFlags(trail);
        add("Trail", trail);
    }
}

void drawRemoveFocusedEmitter(Scene& scene, TugelaEditorState& state, ParticleEmitterUiContext& ctx) {
    if (state.focus_emitter == 0 || state.focus_system == 0) {
        return;
    }
    const Node* emitter = scene.find(state.focus_emitter);
    if (!emitter || !emitter->particle_emitter.has_value()) {
        return;
    }
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.10f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.40f, 0.14f, 0.14f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.08f, 0.08f, 1.0f));
    if (ImGui::Button("Remove Emitter", ImVec2(-1.0f, 0))) {
        const NodeId removed = state.focus_emitter;
        const NodeId system_id = state.focus_system;
        if (Node* system = scene.find(system_id)) {
            system->is_particle_system = true;
        }
        scene.deleteNode(removed);
        std::vector<NodeId> remaining;
        collectEmitters(scene, system_id, remaining);
        state.focus_emitter = remaining.empty() ? 0 : remaining.front();
        state.focus_system = system_id;
        if (ctx.select_node) {
            ctx.select_node(system_id);
        }
        mark(ctx, true); // cull orphan particles + resync emitter runtimes
    }
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Delete this emitter from the System. Add templates below to restore.");
    }
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
    const TugelaMetrics m = makeMetrics();

    const float min_w = std::max(720.0f, m.font * 42.0f);
    const float min_h = std::max(480.0f, m.font * 28.0f);
    ImGui::SetNextWindowSize(ImVec2(min_w * 1.35f, min_h * 1.35f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(min_w, min_h), ImVec2(3200.0f, 2200.0f));
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

    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        draw->AddRectFilled(p, ImVec2(p.x + w, p.y + m.header_h), IM_COL32(18, 20, 24, 255));
        draw->AddRectFilled(p, ImVec2(p.x + 3.0f, p.y + m.header_h), IM_COL32(86, 198, 208, 255));
        ImGui::Dummy(ImVec2(w, m.header_h));
        ImGui::SetCursorScreenPos(ImVec2(p.x + m.pad * 1.4f, p.y + (m.header_h - m.font) * 0.5f));
        ImGui::TextColored(kAccent, "Tugela");
        ImGui::SameLine(0.0f, m.pad);
        ImGui::TextColored(ImVec4(0.88f, 0.90f, 0.93f, 1.0f), "System · Emitters · Module stack");
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + m.header_h + 4.0f));
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(m.pad, m.pad));
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

    ImGui::BeginChild("tugela_systems", ImVec2(m.systems_w, 0.0f), ImGuiChildFlags_Borders);
    drawEditorSectionLabel("Systems");
    ImGui::TextDisabled("World actors");
    ImGui::Spacing();
    if (systems.empty()) {
        ImGui::TextWrapped("No systems. Hierarchy → Particle System.");
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

    ImGui::BeginChild("tugela_emitters", ImVec2(m.emitters_w, 0.0f), ImGuiChildFlags_Borders);
    drawEditorSectionLabel("Emitters");
    if (state.focus_system != 0) {
        if (const Node* sys = scene.find(state.focus_system)) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(kMuted, "in %s", sys->name.c_str());
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::Spacing();
    if (emitters.empty()) {
        ImGui::TextDisabled("Empty system — add an emitter below.");
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
                // Keep Hierarchy on the System; emitter authoring stays in Tugela.
                ctx.select_node(state.focus_system != 0 ? state.focus_system : id);
            }
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::Separator();
    if (state.focus_system != 0) {
        drawRemoveFocusedEmitter(scene, state, ctx);
        drawAddEmitterButtons(scene, state.focus_system, ctx, state);
        collectEmitters(scene, state.focus_system, emitters);
    }
    ImGui::EndChild();

    ImGui::SameLine();

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
        const float graph_h = std::max(m.font * 12.0f, ImGui::GetContentRegionAvail().y * 0.55f);
        ImGui::BeginChild("graph_wrap", ImVec2(0.0f, graph_h), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
        drawModuleGraph(state, scene, emitters, ctx, m);
        ImGui::EndChild();

        ImGui::Spacing();
        Node* emitter = scene.find(state.focus_emitter);
        if (emitter && emitter->particle_emitter.has_value()) {
            const std::string path = nodePath(scene, emitter->id);
            ImGui::BeginChild("graph_module_insp", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
            ImGui::TextColored(ImVec4(0.90f, 0.92f, 0.94f, 1.0f), "%s", emitter->name.c_str());
            ImGui::Spacing();
            ImGui::TextColored(kMuted, "%s  ·  %s", stageLabel(state.focus_module),
                               stackLabel(state.focus_stack));
            ImGui::Spacing();
            drawStackModuleInspector(*emitter->particle_emitter, state.focus_stack, ctx, path,
                                     &state);
            ImGui::EndChild();
        } else {
            ImGui::TextDisabled("Select a stack module to edit parameters.");
        }
    } else {
        Node* emitter = scene.find(state.focus_emitter);
        if (!emitter || !emitter->particle_emitter.has_value()) {
            ImGui::TextDisabled("Select an emitter to edit modules, timings, and curves.");
        } else {
            ParticleEmitter& e = *emitter->particle_emitter;
            const std::string path = nodePath(scene, emitter->id);

            ImGui::TextColored(ImVec4(0.90f, 0.92f, 0.94f, 1.0f), "%s", emitter->name.c_str());
            ImGui::SameLine(0.0f, m.pad);
            ImGui::TextColored(kMuted, "emitter");
            ImGui::Spacing();

            drawTimingStrip(e, ctx, m);
            ImGui::Spacing();
            drawCurvePanel(e, ctx, m);
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
