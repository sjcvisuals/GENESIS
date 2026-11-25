// Header-only smoke for actor visibility precedence (no Vulkan).
#include "scene/Scene.hpp"
#include "scene/SceneVisibility.hpp"

#include <cstdio>

namespace {

bool expect(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "visibility_smoke: FAIL %s\n", msg);
        return false;
    }
    return true;
}

} // namespace

int main() {
    using genesis::NodeId;
    using genesis::Scene;
    using genesis::isNodeDrawnInView;

    Scene scene{};
    const NodeId root = scene.createEmptyNode("Root");
    const NodeId child = scene.createEmptyNode("Child", root);
    const NodeId grand = scene.createEmptyNode("Grand", child);

    bool ok = true;
    ok &= expect(isNodeDrawnInView(scene, *scene.find(grand), false), "default editor draw");
    ok &= expect(isNodeDrawnInView(scene, *scene.find(grand), true), "default game draw");

    scene.find(grand)->visible = false;
    ok &= expect(!isNodeDrawnInView(scene, *scene.find(grand), false), "self Visible=off");
    ok &= expect(!isNodeDrawnInView(scene, *scene.find(grand), true), "self Visible=off in game");
    scene.find(grand)->visible = true;

    scene.find(root)->visible = false;
    ok &= expect(!isNodeDrawnInView(scene, *scene.find(grand), false), "ancestor Visible=off");
    scene.find(root)->visible = true;

    scene.find(grand)->hidden_in_game = true;
    ok &= expect(isNodeDrawnInView(scene, *scene.find(grand), false), "HG still drawn in editor");
    ok &= expect(!isNodeDrawnInView(scene, *scene.find(grand), true), "HG hidden in game output");

    scene.find(grand)->visible = false;
    ok &= expect(!isNodeDrawnInView(scene, *scene.find(grand), false), "Visible wins over HG in editor");
    ok &= expect(!isNodeDrawnInView(scene, *scene.find(grand), true), "Visible wins over HG in game");
    scene.find(grand)->visible = true;

    scene.find(root)->hidden_in_game = true;
    scene.find(grand)->hidden_in_game = false;
    ok &= expect(isNodeDrawnInView(scene, *scene.find(grand), false), "ancestor HG still in editor");
    ok &= expect(!isNodeDrawnInView(scene, *scene.find(grand), true), "ancestor HG hides in game");

    if (!ok) {
        return 1;
    }
    std::printf("visibility_smoke: ok (Visible > Hidden-in-Game, ancestry, no selection override)\n");
    return 0;
}
