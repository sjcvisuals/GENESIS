#pragma once

#include "animation/AnimationClip.hpp"
#include "particles/FluidVolume.hpp"
#include "particles/ParticleEmitter.hpp"
#include "scene/SceneCamera.hpp"
#include "scene/SceneLight.hpp"
#include "scene/SceneMaterial.hpp"
#include "scene/Transform.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace genesis {

// Protocol / OSC listen endpoint authored in the editor and restored for player / Pixera.
struct SceneOscSettings {
    std::string bind_ip = "0.0.0.0";
    std::uint16_t port = 9000;
    bool listening = false;
};

struct SceneDocumentNode {
    std::string path;
    std::string name;
    std::optional<std::string> parent_path;
    Transform local{};
    bool visible = true;
    bool pickable = true;
    bool hidden_in_game = false;
    bool collision_enabled = true;
    bool is_particle_system = false;

    std::optional<std::string> mesh_ref;
    std::optional<std::string> material_ref;

    std::optional<SceneCamera> camera;
    std::optional<SceneLight> light;
    std::optional<ParticleEmitter> particle_emitter;
    std::optional<FluidVolume> particle_fluid;
};

struct SceneDocument {
    int version = 1;
    std::string source; // glTF path relative to assets, or "builtin"
    std::optional<std::string> active_output_camera;
    // When set, editor / ExtGenesis restore UDP listen IP + port from this scene.
    std::optional<SceneOscSettings> osc;

    std::vector<std::pair<std::string, SceneMaterial>> materials;
    std::vector<SceneDocumentNode> nodes;
    std::vector<AnimationClip> animations;
};

} // namespace genesis
