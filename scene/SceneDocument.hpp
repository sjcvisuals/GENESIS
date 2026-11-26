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

// People / room tracking authored with the scene (Medusa example + from-scratch installs).
// Visible in View → Room Tracking — not silent filename magic.
struct SceneRoomTrackingSettings {
    bool enabled = false;
    std::string address = "/genesis/room/person";

    bool tuio_enabled = false;
    std::string tuio_address = "/tuio/2Dcur";
    float tuio_scale = 3.0f;
    float tuio_offset_x = 0.0f;
    float tuio_offset_y = 0.0f;

    std::string person_pool_path = "Scene/Person Pool";
    std::string wall_plane_path = "Scene/Wall Plane";
    std::uint32_t max_persons = 10;
    float slot_idle_seconds = 0.75f;

    float room_radius = 3.0f;
    float tank_radius = 1.0f;
    float feet_height = 0.05f;
    float wall_spawn_height = 0.12f;
    float wall_ray_max_distance = 20.0f;
    // "fixed_distance" | "plane_collision" | "prefer_plane_then_distance"
    std::string wall_ray_mode = "fixed_distance";
    bool debug_draw_wall_ray = false;

    bool track_feet_emitter = true;
    bool track_wall_emitter = true;
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
    // When set, restore people / wall-ray tracking (authorable Room Tracking panel).
    std::optional<SceneRoomTrackingSettings> room_tracking;

    std::vector<std::pair<std::string, SceneMaterial>> materials;
    std::vector<SceneDocumentNode> nodes;
    std::vector<AnimationClip> animations;
};

} // namespace genesis
