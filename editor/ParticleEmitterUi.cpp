#include "editor/ParticleEmitterUi.hpp"

#include "editor/UiWrap.hpp"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

namespace genesis {

namespace {

constexpr ImVec4 kAccent{0.30f, 0.74f, 0.78f, 1.0f};
constexpr ImVec4 kMuted{0.48f, 0.505f, 0.545f, 1.0f};
constexpr ImVec4 kModuleTitle{0.86f, 0.89f, 0.92f, 1.0f};

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

bool moduleHeader(const char* label, bool default_open = true) {
    const ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_AllowOverlap |
        ImGuiTreeNodeFlags_SpanAvailWidth |
        (default_open ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.062f, 0.070f, 0.080f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.090f, 0.110f, 0.122f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.080f, 0.120f, 0.130f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, kModuleTitle);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
    const bool open = ImGui::CollapsingHeader(label, flags);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    return open;
}

void moduleIndentBegin() {
    ImGui::Indent(10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
    ImGui::Spacing();
}

void moduleIndentEnd() {
    ImGui::PopStyleVar();
    ImGui::Unindent(10.0f);
    ImGui::Spacing();
}

int countEmitterChildren(const Scene& scene, const Node& node) {
    int count = 0;
    for (const NodeId child : node.children) {
        if (const Node* c = scene.find(child); c && c->particle_emitter.has_value()) {
            ++count;
        }
    }
    return count;
}

} // namespace

bool isParticleSystemNode(const Scene& scene, const Node& node) {
    if (node.particle_emitter.has_value()) {
        return false;
    }
    if (node.mesh_resource.has_value() || node.camera.has_value() || node.light.has_value()) {
        return false;
    }
    if (node.is_particle_system) {
        return true;
    }
    // Legacy: systems authored before the persistent flag still qualify via children.
    return countEmitterChildren(scene, node) > 0;
}

bool isHierarchyHiddenParticleEmitter(const Node& node) {
    return node.particle_emitter.has_value();
}

const char* particleHierarchyBadge(const Scene& scene, const Node& node) {
    if (isHierarchyHiddenParticleEmitter(node)) {
        return "";
    }
    if (isParticleSystemNode(scene, node)) {
        return "SYS";
    }
    return "";
}

void drawParticleSystemInspector(Node& system_node, ParticleEmitterUiContext& ctx) {
    if (!ctx.scene) {
        return;
    }
    Scene& scene = *ctx.scene;

    ImGui::Spacing();
    ImGui::TextColored(kAccent, "Tugela System");
    ImGui::TextColored(kMuted, "System → Emitter hierarchy · lean basics");
    ImGui::Spacing();

    const int emitters = countEmitterChildren(scene, system_node);
    ImGui::Text("Emitters  ·  %d", emitters);
    ImGui::Spacing();

    if (moduleHeader("Emitter Stack", true)) {
        moduleIndentBegin();
        if (emitters == 0) {
            ImGui::TextDisabled("No emitters yet — add a template below.");
        }
        bool any_spawn_debug = false;
        bool all_spawn_debug = emitters > 0;
        for (const NodeId child : system_node.children) {
            Node* c = scene.find(child);
            if (!c || !c->particle_emitter.has_value()) {
                continue;
            }
            ImGui::PushID(static_cast<int>(child));
            const bool enabled = c->particle_emitter->enabled;
            any_spawn_debug = any_spawn_debug || c->particle_emitter->debug_show_spawn;
            all_spawn_debug = all_spawn_debug && c->particle_emitter->debug_show_spawn;
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::TextUnformatted(c->name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled(enabled ? "on" : "off");
            ImGui::SameLine();
            ImGui::TextDisabled("· Tugela");
            ImGui::PopID();
        }
        if (emitters > 0) {
            ImGui::Spacing();
            bool show_spawn = all_spawn_debug;
            if (ImGui::Checkbox("Show Spawn Location (all emitters)", &show_spawn)) {
                for (const NodeId child : system_node.children) {
                    Node* c = scene.find(child);
                    if (!c || !c->particle_emitter.has_value()) {
                        continue;
                    }
                    c->particle_emitter->debug_show_spawn = show_spawn;
                }
                mark(ctx, false);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Toggle spawn-origin markers on every emitter under this system. "
                    "Hidden in Game View (G) and Pixera output.");
            }
            if (any_spawn_debug && !all_spawn_debug) {
                ImGui::SameLine();
                ImGui::TextDisabled("(mixed)");
            }
        }
        moduleIndentEnd();
    }

    if (moduleHeader("Add Emitter", true)) {
        moduleIndentBegin();
        ImGui::TextDisabled("Templates default to soft circles (gentle look).");
        const float longest = ImGui::CalcTextSize("Circles+Trail").x;
        const int cols = uiAdaptiveColumns(3, longest);
        const float btn_w = uiButtonWidthForColumns(cols);
        int bi = 0;

        auto addEmitter = [&](const char* name, const ParticleEmitter& preset) {
            system_node.is_particle_system = true;
            scene.createParticleEmitterNode(name, system_node.id, preset);
            mark(ctx, true); // rebuild so new emitter is live immediately
            if (ctx.select_node) {
                // Keep Hierarchy selection on the System; emitters are Tugela-only.
                ctx.select_node(system_node.id);
            }
        };
        auto placeBtn = [&](const char* label, auto&& fn) {
            if (cols > 1 && bi > 0 && (bi % cols) != 0) {
                ImGui::SameLine();
            }
            if (ImGui::Button(label, ImVec2(btn_w, 0))) {
                fn();
            }
            ++bi;
        };

        placeBtn("Soft Circles", [&] { addEmitter("Soft Circles", makeGentleCirclesEmitter()); });
        placeBtn("Trail", [&] {
            ParticleEmitter trail = makeGentleSoftEmitter();
            trail.rate_per_second = 6.0f;
            particleStackSet(trail.module_stack, kStackTrail, true);
            particleStackSet(trail.module_enabled, kStackTrail, true);
            particleStackSet(trail.module_stack, kStackGravity, true);
            particleStackSet(trail.module_enabled, kStackGravity, true);
            syncOptionalStackFlags(trail);
            addEmitter("Trail", trail);
        });
        placeBtn("Circles+Trail", [&] {
            addEmitter("Soft Circles", makeGentleCirclesEmitter());
            ParticleEmitter trail = makeGentleSoftEmitter();
            trail.rate_per_second = 5.0f;
            particleStackSet(trail.module_stack, kStackTrail, true);
            particleStackSet(trail.module_enabled, kStackTrail, true);
            syncOptionalStackFlags(trail);
            addEmitter("Trail", trail);
        });
        moduleIndentEnd();
    }
}

void drawParticleEmitterInspector(Node& emitter_node, const std::string& node_path,
                                  ParticleEmitterUiContext& ctx) {
    if (!emitter_node.particle_emitter.has_value()) {
        return;
    }
    ParticleEmitter& e = *emitter_node.particle_emitter;
    bool dirty = false;

    ImGui::Separator();
    ImGui::TextColored(kAccent, "Tugela Emitter");
    ImGui::TextColored(kMuted, "Module stack · Spawn → Initialize → Update → Render");

    if (ctx.scene && emitter_node.parent.has_value()) {
        if (const Node* parent = ctx.scene->find(*emitter_node.parent)) {
            const int siblings = countEmitterChildren(*ctx.scene, *parent);
            ImGui::TextDisabled("System \"%s\"  ·  %d emitter(s)", parent->name.c_str(), siblings);
            if (ctx.select_node) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Select System")) {
                    ctx.select_node(*emitter_node.parent);
                }
            }
        }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Templates");
    if (ImGui::SmallButton("Soft Circles")) {
        e = makeGentleCirclesEmitter();
        dirty = true;
    }
    uiSameLineIfFits(ImGui::CalcTextSize("Soft + Trails").x + ImGui::GetStyle().FramePadding.x * 2.0f);
    if (ImGui::SmallButton("Soft + Trails")) {
        e = makeGentleSoftEmitter();
        dirty = true;
    }
    ImGui::Spacing();
    ImGui::TextDisabled("OSC: link params via Protocol window, or add OSC Drive in Tugela.");
    ImGui::Spacing();

    // --- Emitter Properties ---
    if (moduleHeader("Emitter Properties", true)) {
        moduleIndentBegin();
        dirty |= ImGui::Checkbox("Enabled", &e.enabled);
        if (ctx.osc) {
            drawOscLinkButton(*ctx.osc, OscParamTarget::ParticleEnabled, node_path, 0, "p_en");
        }
        {
            const float need = ImGui::CalcTextSize("Looping").x + ImGui::GetFontSize() + 12.0f;
            uiSameLineIfFits(need);
        }
        dirty |= ImGui::Checkbox("Looping", &e.looping);
        {
            const float need = ImGui::CalcTextSize("World space").x + ImGui::GetFontSize() + 12.0f;
            uiSameLineIfFits(need);
        }
        dirty |= ImGui::Checkbox("World space", &e.world_space);
        dirty |= ImGui::Checkbox("Show Spawn Location", &e.debug_show_spawn);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Draw a spawn-origin marker at this emitter's transform in the editor viewport. "
                "Hidden in Game View (G) and Pixera output.");
        }
        dirty |= ImGui::DragFloat("Duration (0=∞)", &e.duration_seconds, 0.05f, 0.0f, 600.0f);
        dirty |= ImGui::DragFloat("Start delay", &e.start_delay_seconds, 0.01f, 0.0f, 60.0f);
        int max_p = static_cast<int>(e.max_particles);
        if (ImGui::DragInt("Max particles", &max_p, 1, 1, 65536)) {
            e.max_particles = static_cast<std::uint32_t>(std::max(max_p, 1));
            dirty = true;
        }
        if (ctx.osc) {
            drawOscLinkButton(*ctx.osc, OscParamTarget::ParticleMax, node_path, 0, "p_max");
        }
        int seed = static_cast<int>(e.seed_override);
        if (ImGui::DragInt("Seed override (0=scene)", &seed, 1, 0, 1000000)) {
            e.seed_override = static_cast<std::uint64_t>(std::max(seed, 0));
            dirty = true;
        }
        moduleIndentEnd();
    }

    // --- 1 Spawn Rate ---
    if (moduleHeader("1  ·  Spawn Rate", true)) {
        moduleIndentBegin();
        dirty |= ImGui::DragFloat("Rate / s", &e.rate_per_second, 0.5f, 0.0f, 20000.0f);
        if (ctx.emitter_live) {
            const EmitterLiveUiStats live = ctx.emitter_live(emitter_node.id);
            if (live.found) {
                ImGui::TextDisabled("Alive %u  ·  Running %.1f / s", live.alive,
                                    live.measured_spawn_rate);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Measured spawn rate over ~0.25 s (actual births/sec). "
                        "Compare to Rate / s when diagnosing hitching.");
                }
            }
        }
        if (ctx.osc) {
            drawOscLinkButton(*ctx.osc, OscParamTarget::ParticleRate, node_path, 0, "p_rate");
        }
        if (ctx.audio) {
            drawAudioLinkButton(*ctx.audio, OscParamTarget::ParticleRate, node_path, 0, "p_rate");
        }
        dirty |= ImGui::DragFloat("Spawn / meter", &e.spawn_per_unit, 0.1f, 0.0f, 500.0f);
        int burst = static_cast<int>(e.burst_count);
        if (ImGui::DragInt("Burst @ t=0", &burst, 1, 0, 100000)) {
            e.burst_count = static_cast<std::uint32_t>(std::max(burst, 0));
            dirty = true;
        }
        if (ctx.osc) {
            drawOscLinkButton(*ctx.osc, OscParamTarget::ParticleBurst, node_path, 0, "p_burst");
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
        moduleIndentEnd();
    }

    // --- 2 Initialize Particle ---
    if (moduleHeader("2  ·  Initialize Particle", true)) {
        moduleIndentBegin();
        dirty |= ImGui::DragFloatRange2("Lifetime", &e.lifetime_min, &e.lifetime_max, 0.01f, 0.01f, 60.0f);
        if (ctx.osc) {
            drawOscLinkButton(*ctx.osc, OscParamTarget::ParticleLifetimeMin, node_path, 0, "p_life_min");
            drawOscLinkButton(*ctx.osc, OscParamTarget::ParticleLifetimeMax, node_path, 0, "p_life_max");
        }
        dirty |= ImGui::DragFloatRange2("Speed", &e.speed_min, &e.speed_max, 0.01f, 0.0f, 100.0f);
        if (ctx.osc) {
            drawOscLinkButton(*ctx.osc, OscParamTarget::ParticleSpeedMin, node_path, 0, "p_spd_min");
            drawOscLinkButton(*ctx.osc, OscParamTarget::ParticleSpeedMax, node_path, 0, "p_spd_max");
        }
        dirty |= ImGui::DragFloatRange2("Size", &e.size_min, &e.size_max, 0.001f, 0.02f, 10.0f);
        if (ctx.osc) {
            drawOscLinkButton(*ctx.osc, OscParamTarget::ParticleSizeMin, node_path, 0, "p_sz_min");
            drawOscLinkButton(*ctx.osc, OscParamTarget::ParticleSizeMax, node_path, 0, "p_sz_max");
        }
        dirty |= ImGui::DragFloat("Size end mul", &e.size_end_multiplier, 0.01f, 0.0f, 2.0f);
        dirty |= ImGui::ColorEdit4("Color start", &e.color_start.x,
                                   ImGuiColorEditFlags_AlphaPreviewHalf);
        dirty |= ImGui::ColorEdit4("Color end", &e.color_end.x,
                                   ImGuiColorEditFlags_AlphaPreviewHalf);

        ImGui::Spacing();
        ImGui::TextDisabled("Spawn shape / velocity");
        const char* shapes[] = {"Point", "Sphere", "Cone", "Box", "Ring (cylinder)"};
        int shape = static_cast<int>(e.spawn_shape);
        if (ImGui::Combo("Spawn shape", &shape, shapes, IM_ARRAYSIZE(shapes))) {
            e.spawn_shape = static_cast<ParticleSpawnShape>(shape);
            dirty = true;
        }
        dirty |= ImGui::DragFloat("Spawn radius", &e.spawn_radius, 0.01f, 0.0f, 50.0f);
        dirty |= ImGui::DragFloat("Spawn height / box Y", &e.spawn_box_height, 0.01f, 0.0f, 50.0f);
        dirty |= ImGui::DragFloat("Cone angle", &e.cone_angle_degrees, 0.5f, 0.0f, 89.0f);
        const char* vel_modes[] = {"Cone", "From point", "Random", "Horizontal plane"};
        int vel_mode = static_cast<int>(e.velocity_mode);
        if (ImGui::Combo("Velocity mode", &vel_mode, vel_modes, IM_ARRAYSIZE(vel_modes))) {
            e.velocity_mode = static_cast<ParticleVelocityMode>(vel_mode);
            dirty = true;
        }
        dirty |= ImGui::DragFloat("Inherit velocity", &e.inherit_velocity, 0.01f, 0.0f, 1.0f);
        moduleIndentEnd();
    }

    // --- 3 Particle Update ---
    if (moduleHeader("3  ·  Particle Update", true)) {
        moduleIndentBegin();
        if (ImGui::Checkbox("Gravity / Lift", &e.force_gravity)) {
            setOptionalModuleArmed(e, kStackGravity, e.force_gravity);
            dirty = true;
        }
        if (e.force_gravity) {
            dirty |= ImGui::DragFloat3("Gravity (+Y = lift)", &e.gravity.x, 0.02f);
            if (ctx.osc) {
                drawOscLinkButton(*ctx.osc, OscParamTarget::ParticleGravityY, node_path, 0, "p_gy");
            }
            if (ctx.audio) {
                drawAudioLinkButton(*ctx.audio, OscParamTarget::ParticleGravityY, node_path, 0, "p_gy");
            }
        }
        if (ImGui::Checkbox("Drag", &e.force_drag)) {
            setOptionalModuleArmed(e, kStackDrag, e.force_drag);
            dirty = true;
        }
        if (e.force_drag) {
            dirty |= ImGui::DragFloat("Drag amount", &e.drag, 0.01f, 0.0f, 10.0f);
        }

        // Advanced forces stay collapsed — keeps the stack readable.
        if (ImGui::TreeNodeEx("Advanced forces", ImGuiTreeNodeFlags_Framed)) {
            if (ImGui::Checkbox("Point force", &e.force_point)) {
                setOptionalModuleArmed(e, kStackPointForce, e.force_point);
                dirty = true;
            }
            if (e.force_point) {
                dirty |= ImGui::DragFloat3("Point position", &e.point_force_position.x, 0.05f);
                dirty |= ImGui::DragFloat("Strength (+in/-out)", &e.point_force_strength, 0.1f, -200.0f,
                                         200.0f);
            }
            if (ImGui::Checkbox("Vortex / Orbit", &e.force_vortex)) {
                setOptionalModuleArmed(e, kStackVortex, e.force_vortex);
                dirty = true;
            }
            if (e.force_vortex) {
                dirty |= ImGui::DragFloat("Vortex strength", &e.vortex_strength, 0.05f, -50.0f, 50.0f);
                dirty |= ImGui::DragFloat3("Vortex axis", &e.vortex_axis.x, 0.05f);
                dirty |= ImGui::DragFloat("Vortex radius", &e.vortex_radius, 0.05f, 0.1f, 100.0f);
            }
            if (ImGui::Checkbox("Noise", &e.force_noise)) {
                setOptionalModuleArmed(e, kStackNoise, e.force_noise);
                dirty = true;
            }
            if (e.force_noise) {
                ImGui::TextDisabled("Keep low — noise sparkles on soft additive sprites");
                dirty |= ImGui::DragFloat("Noise strength", &e.noise_strength, 0.005f, 0.0f, 5.0f);
                dirty |= ImGui::DragFloat("Noise frequency", &e.noise_frequency, 0.01f, 0.01f, 20.0f);
            }
            if (ImGui::Checkbox("Acceleration", &e.force_accel)) {
                setOptionalModuleArmed(e, kStackAccel, e.force_accel);
                dirty = true;
            }
            if (e.force_accel) {
                dirty |= ImGui::DragFloat3("Acceleration", &e.acceleration.x, 0.05f);
            }
            if (ImGui::Checkbox("Kill below plane Y", &e.kill_plane)) {
                setOptionalModuleArmed(e, kStackKillPlane, e.kill_plane);
                dirty = true;
            }
            if (e.kill_plane) {
                dirty |= ImGui::DragFloat("Kill plane Y", &e.kill_plane_y, 0.05f);
            }
            ImGui::TreePop();
        }
        moduleIndentEnd();
    }

    // --- 4 Scale / Color over Life ---
    if (moduleHeader("4  ·  Scale / Color over Life", true)) {
        moduleIndentBegin();
        if (ImGui::Button("Rebuild curves from endpoints")) {
            rebuildEmitterCurvesFromEndpoints(e);
            dirty = true;
        }
        int size_keys = static_cast<int>(e.size_over_life.count);
        if (ImGui::DragInt("Size keys", &size_keys, 1, 0, 4)) {
            e.size_over_life.count = static_cast<std::uint8_t>(std::clamp(size_keys, 0, 4));
            dirty = true;
        }
        for (std::uint8_t ki = 0; ki < e.size_over_life.count; ++ki) {
            ImGui::PushID(100 + ki);
            dirty |= ImGui::DragFloat("t", &e.size_over_life.keys[ki].t, 0.01f, 0.0f, 1.0f);
            ImGui::SameLine();
            dirty |= ImGui::DragFloat("size mul", &e.size_over_life.keys[ki].v, 0.01f, 0.0f, 8.0f);
            ImGui::PopID();
        }
        int color_keys = static_cast<int>(e.color_over_life.count);
        if (ImGui::DragInt("Color keys", &color_keys, 1, 0, 4)) {
            e.color_over_life.count = static_cast<std::uint8_t>(std::clamp(color_keys, 0, 4));
            dirty = true;
        }
        for (std::uint8_t ki = 0; ki < e.color_over_life.count; ++ki) {
            ImGui::PushID(200 + ki);
            dirty |= ImGui::DragFloat("t##c", &e.color_over_life.keys[ki].t, 0.01f, 0.0f, 1.0f);
            dirty |= ImGui::ColorEdit4("rgba", &e.color_over_life.keys[ki].v.x,
                                       ImGuiColorEditFlags_AlphaPreviewHalf);
            ImGui::PopID();
        }
        moduleIndentEnd();
    }

    // --- 5 Sprite Renderer ---
    if (moduleHeader("5  ·  Sprite Renderer", true)) {
        moduleIndentBegin();
        dirty |= ImGui::Checkbox("Enable sprites", &e.mod_sprite);
        dirty |= ImGui::Checkbox("Unlit", &e.unlit);
        ImGui::SameLine();
        dirty |= ImGui::Checkbox("Soft additive", &e.additive);
        ImGui::TextDisabled("Soft circle sprite · bloom-safe gaussian alpha");
        if (e.mod_sprite) {
            dirty |= ImGui::DragFloat("Sprite aspect", &e.sprite_aspect, 0.01f, 0.05f, 8.0f);
        }
        moduleIndentEnd();
    }

    // --- 5b Mesh Renderer ---
    if (moduleHeader("5b ·  Mesh Renderer", true)) {
        moduleIndentBegin();
        dirty |= ImGui::Checkbox("Enable mesh head", &e.mod_mesh);
        ImGui::TextDisabled("3D Cube / Sphere · off with Sprite/Trail = invisible particles");
        if (e.mod_mesh) {
            const char* meshes[] = {"Cube", "Sphere"};
            int mesh = (e.render_mesh == ParticleRenderMesh::Cube) ? 0 : 1;
            if (ImGui::Combo("Mesh", &mesh, meshes, IM_ARRAYSIZE(meshes))) {
                e.render_mesh =
                    (mesh == 0) ? ParticleRenderMesh::Cube : ParticleRenderMesh::Sphere;
                dirty = true;
            }
        }
        moduleIndentEnd();
    }

    // --- 6 Trail Renderer ---
    if (moduleHeader("6  ·  Trail Renderer", true)) {
        moduleIndentBegin();
        if (ImGui::Checkbox("Enable trails", &e.trail_enabled)) {
            setOptionalModuleArmed(e, kStackTrail, e.trail_enabled);
            dirty = true;
        }
        if (e.trail_enabled) {
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
                "Live: width / alpha / segments / sample / lifetime · disable stops draw.");
        }
        moduleIndentEnd();
    }

    if (dirty) {
        syncEmitterCurvesFromEndpoints(e);
        mark(ctx); // live-apply trail / module edits to living particles
    }

    if (ctx.stats) {
        const ParticleWorldStatsLite stats = ctx.stats();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Alive %u  ·  Emitters %u  ·  Steps %u", stats.alive, stats.emitters,
                            stats.steps);
    }
    if (ctx.reset_particles && ImGui::Button("Restart Particles", ImVec2(-1, 0))) {
        ctx.reset_particles();
    }
}

} // namespace genesis
