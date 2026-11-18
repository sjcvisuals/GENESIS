#pragma once

// Lean Tugela Editor pop-out — System (one level actor) → multiple Emitters →
// module node graph + timings + curves. Niagara-like, not a full Unreal clone.

#include "editor/ParticleEmitterUi.hpp"

#include <cstdint>

namespace genesis {

enum class TugelaEditorTab : std::uint8_t {
    Details = 0,
    Graph = 1,
};

enum class TugelaModuleKind : std::uint8_t {
    Spawn = 0,
    Initialize = 1,
    Update = 2,
    OverLife = 3,
    Sprite = 4,
    Trail = 5,
    Count = 6,
};

struct TugelaEditorState {
    bool open = false;
    TugelaEditorTab tab = TugelaEditorTab::Graph;
    NodeId focus_system = 0;
    NodeId focus_emitter = 0;
    TugelaModuleKind focus_module = TugelaModuleKind::Spawn;
    float graph_pan_x = 20.0f;
    float graph_pan_y = 24.0f;
};

// Floating Tugela Editor window. Safe to call every frame; respects state.open.
void drawTugelaEditorWindow(TugelaEditorState& state, ParticleEmitterUiContext& ctx);

// Open (or focus) the editor on a system / emitter selection.
void tugelaEditorFocusSelection(TugelaEditorState& state, Scene& scene, NodeId selected);

} // namespace genesis
