#include "runtime/DeterministicAnimation.hpp"

#include "scene/ScenePath.hpp"

#include <glm/gtc/quaternion.hpp>
#include <cmath>

namespace genesis {

namespace {

Node* findAnimationTarget(Scene& scene, const std::string& node_path) {
    if (!node_path.empty()) {
        if (std::optional<NodeId> id = findNodeByPath(scene, node_path)) {
            return scene.find(*id);
        }
        return nullptr;
    }

    for (Node& node : scene.nodes) {
        if (node.mesh_resource.has_value()) {
            return &node;
        }
    }
    return scene.nodes.empty() ? nullptr : &scene.nodes.front();
}

} // namespace

void applyDeterministicAnimation(Scene& scene, const GenesisFrameTime& frame_time,
                                 const DeterministicAnimationConfig& config) {
    if (frame_time.is_paused) {
        return;
    }

    Node* node = findAnimationTarget(scene, config.node_path);
    if (!node) {
        return;
    }

    const float angle =
        static_cast<float>(frame_time.production_time_seconds) * config.rotation_radians_per_second;
    const glm::vec3 axis = glm::normalize(config.rotation_axis);
    node->local.rotation = glm::angleAxis(angle, axis);

    if (config.position_amplitude != 0.0f) {
        const float offset =
            config.position_amplitude *
            std::sin(static_cast<float>(frame_time.production_time_seconds) * 2.0f);
        node->local.position = config.position_base + config.position_axis * offset;
    }
}

} // namespace genesis
