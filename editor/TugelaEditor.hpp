#pragma once

// Lean Tugela Editor pop-out — System (one level actor) → multiple Emitters →
// one Niagara-like module stack per emitter (Spawn / Update / Render sections,
// leaf enable) + timings + curves. Leaf modules are not separate graph nodes.

#include "editor/ParticleEmitterUi.hpp"

#include <cstdint>

namespace genesis {

enum class TugelaEditorTab : std::uint8_t {
    Details = 0,
    Graph = 1,
};

// Category sections inside a single emitter module stack (not separate nodes).
enum class TugelaModuleKind : std::uint8_t {
    Spawn = 0,
    Update = 1,
    Render = 2,
    Count = 3,
};

// Leaf modules listed under a category section (Niagara-like stack entries).
// Keep in sync with ParticleEmitter runtime flags — every supported module must
// appear under stageForStack() and in the section "+" add directory.
enum class TugelaStackMod : std::uint8_t {
    SpawnRate = 0,
    Burst,
    Shape,
    SpawnByDistance, // spawn_per_unit (meters traveled)
    Lifetime,
    Velocity,
    Size,
    Color,
    Gravity,
    Drag,
    PointForce,
    Vortex,
    Noise,
    Accel,
    KillPlane,
    KillBox,
    FluidCoupled,
    SizeOverLife,
    ColorOverLife,
    ScaleBySpeed,
    Sprite,
    Mesh,   // 3D Cube/Sphere head renderer
    Trail,
    OscDrive, // link Protocol XY / params to this emitter (not a special actor)
    Count,
};

struct TugelaEditorState {
    bool open = false;
    TugelaEditorTab tab = TugelaEditorTab::Graph;
    NodeId focus_system = 0;
    NodeId focus_emitter = 0;
    TugelaModuleKind focus_module = TugelaModuleKind::Spawn;
    TugelaStackMod focus_stack = TugelaStackMod::SpawnRate;
    // Category for the open "+" module-directory popup (must outlive the click frame).
    TugelaModuleKind add_popup_stage = TugelaModuleKind::Spawn;
    float graph_pan_x = 16.0f;
    float graph_pan_y = 22.0f;
    float graph_zoom = 1.0f; // scroll-wheel zoom for the module graph canvas
};

// Floating Tugela Editor window. Safe to call every frame; respects state.open.
void drawTugelaEditorWindow(TugelaEditorState& state, ParticleEmitterUiContext& ctx);

// Sync Tugela focus from a Hierarchy/viewport selection.
// Emitter selection focuses that emitter; System selection keeps the current
// emitter when it still belongs to the System (so Tugela emitter-row clicks that
// re-select the System do not snap focus back to emitters[0]).
void tugelaEditorFocusSelection(TugelaEditorState& state, Scene& scene, NodeId selected);

} // namespace genesis
