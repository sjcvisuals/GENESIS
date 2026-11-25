#pragma once

// Single source of truth for actor draw / contribution visibility.
// Precedence (first fail wins — no stacking overrides, selection never influences):
//   1. This node or any ancestor has visible == false        → hidden everywhere
//   2. game_output && (node or ancestor has hidden_in_game) → hidden in Game View / output
//   3. otherwise drawn / contributing
//
// Visible  = editor + runtime show/hide (Inspector "Visible").
// Hidden in Game = Unreal-like: still shown in the editor viewport, omitted when
//                  game_output is true (Game View G / Pixera Renderbridge).

#include "scene/Scene.hpp"

namespace genesis {

inline bool isNodeDrawnInView(const Scene& scene, const Node& node, bool game_output) {
    const Node* cur = &node;
    // Guard against a corrupt parent cycle.
    for (std::size_t depth = 0; depth < scene.nodes.size() + 1; ++depth) {
        if (!cur->visible) {
            return false;
        }
        if (game_output && cur->hidden_in_game) {
            return false;
        }
        if (!cur->parent.has_value()) {
            return true;
        }
        const Node* parent = scene.find(*cur->parent);
        if (!parent) {
            return true;
        }
        cur = parent;
    }
    return false;
}

} // namespace genesis
