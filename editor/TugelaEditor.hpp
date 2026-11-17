#pragma once

// Lean Tugela Editor pop-out — System → Emitter → modules + timings + simple curves.
// Niagara-like art-direction surface without a full node graph.

#include "editor/ParticleEmitterUi.hpp"

namespace genesis {

struct TugelaEditorState {
    bool open = false;
    NodeId focus_system = 0;
    NodeId focus_emitter = 0;
};

// Floating Tugela Editor window. Safe to call every frame; respects state.open.
void drawTugelaEditorWindow(TugelaEditorState& state, ParticleEmitterUiContext& ctx);

// Open (or focus) the editor on a system / emitter selection.
void tugelaEditorFocusSelection(TugelaEditorState& state, Scene& scene, NodeId selected);

} // namespace genesis
