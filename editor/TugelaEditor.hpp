#pragma once

// Lean Tugela Editor pop-out — System (one level actor) → multiple Emitters →
// Niagara-like module stack graph (leaf modules with enable) + timings + curves.

#include "editor/ParticleEmitterUi.hpp"

#include <cstdint>

namespace genesis {

enum class TugelaEditorTab : std::uint8_t {
    Details = 0,
    Graph = 1,
};

// Stage columns on the graph (wires between stages).
enum class TugelaModuleKind : std::uint8_t {
    Spawn = 0,
    Initialize = 1,
    Update = 2,
    OverLife = 3,
    Sprite = 4,
    Trail = 5,
    Count = 6,
};

// Leaf modules shown inside stage nodes (Niagara-like stack entries).
enum class TugelaStackMod : std::uint8_t {
    SpawnRate = 0,
    Burst,
    Shape,
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
    SizeOverLife,
    ColorOverLife,
    ScaleBySpeed,
    Sprite,
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
    float graph_pan_x = 16.0f;
    float graph_pan_y = 22.0f;
};

// Floating Tugela Editor window. Safe to call every frame; respects state.open.
void drawTugelaEditorWindow(TugelaEditorState& state, ParticleEmitterUiContext& ctx);

// Open (or focus) the editor on a system / emitter selection.
void tugelaEditorFocusSelection(TugelaEditorState& state, Scene& scene, NodeId selected);

} // namespace genesis
