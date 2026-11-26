#pragma once

#include "particles/FluidVolume.hpp"
#include "particles/ParticleEmitter.hpp"
#include "scene/SceneCamera.hpp"
#include "scene/SceneLight.hpp"
#include "scene/SceneMaterial.hpp"
#include "scene/Transform.hpp"

#include <cstdint>
#include <glm/mat4x4.hpp>
#include <optional>
#include <string>
#include <vector>

namespace genesis {

using NodeId = std::uint64_t;

struct Node {
    NodeId id = 0;
    std::string name;
    std::optional<NodeId> parent;
    std::vector<NodeId> children;

    Transform local{};
    glm::mat4 world{1.0f};
    // Inspector "Visible": hide everywhere (editor + Game View + output). Cascades to children.
    // See scene/SceneVisibility.hpp — selection never overrides this.
    bool visible = true;
    bool pickable = true;
    // Unreal-like: still shown in the editor; omitted when game_output (Game View / Renderbridge).
    // Cascades to children. Visible=off always wins. Collisions / wall-ray hits still apply
    // unless collision_enabled is false (independent of Hidden in Game).
    bool hidden_in_game = false;
    // Per-mesh collider: wall-ray / particle collide / mesh hit tests honor this.
    // Default ON. Independent of Visible / Hidden in Game / Pickable.
    bool collision_enabled = true;

    // Runtime mesh resource index (owned by application mesh pool).
    std::optional<std::uint32_t> mesh_resource;
    std::optional<std::uint32_t> material_resource;

    std::optional<SceneCamera> camera;
    std::optional<SceneLight> light;
    std::optional<ParticleEmitter> particle_emitter;
    std::optional<FluidVolume> particle_fluid;

    // Tugela System actor: holds emitter children authored in the graph, not Hierarchy.
    // Persists when the system has zero emitters so Add Emitter still works.
    bool is_particle_system = false;

    // Import hints / component flags (expanded in later milestones).
    bool has_mesh = false;
    bool has_material = false;
    bool has_camera = false;
    bool has_light = false;
    bool has_particle_emitter = false;
    bool has_particle_fluid = false;
};

struct Scene {
    std::vector<Node> nodes;
    std::vector<NodeId> roots;

    // When set, scene rendering uses this node's camera. Editor navigation stays independent.
    std::optional<NodeId> active_output_camera;

    Node* find(NodeId id);
    const Node* find(NodeId id) const;

    void updateWorldTransforms();

    NodeId createEmptyNode(const std::string& name, std::optional<NodeId> parent = std::nullopt);
    NodeId createMeshNode(const std::string& name, std::uint32_t mesh_resource,
                          std::optional<NodeId> parent = std::nullopt,
                          std::optional<std::uint32_t> material_resource = std::nullopt);
    NodeId createCameraNode(const std::string& name, std::optional<NodeId> parent = std::nullopt);
    NodeId createLightNode(const std::string& name, SceneLightType type,
                           std::optional<NodeId> parent = std::nullopt);
    // Empty world actor marked as a Tugela Particle System (emitters added separately).
    NodeId createParticleSystemNode(const std::string& name,
                                    std::optional<NodeId> parent = std::nullopt);
    NodeId createParticleEmitterNode(const std::string& name,
                                     std::optional<NodeId> parent = std::nullopt,
                                     ParticleEmitter emitter = {});
    NodeId createFluidVolumeNode(const std::string& name, std::optional<NodeId> parent = std::nullopt,
                                 FluidVolume fluid = makeDefaultFluidVolume());
    void setActiveOutputCamera(std::optional<NodeId> node_id);
    const Node* activeOutputCameraNode() const;
    NodeId duplicateNode(NodeId id);
    // Deletes the node. Particle System actors also destroy emitter children (not reparented).
    bool deleteNode(NodeId id);
    bool reparentNode(NodeId child, std::optional<NodeId> new_parent);
    bool isDescendantOf(NodeId node, NodeId potential_ancestor) const;

private:
    NodeId generateId() const;
    void detachFromParent(Node& node);
};

} // namespace genesis
