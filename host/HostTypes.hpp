#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace genesis {

// Shared between host integrations (RenderBridge, future media server).
struct HostBridgeConfig {
    std::string window_title = "GENESIS";
    int width = 1280;
    int height = 720;
    bool resizable = true;
    // Host-driven apps disable editor UI; dev tools can enable stats overlay.
    bool enable_stats_overlay = false;
};

struct HostFrameInput {
    enum class CameraMode : std::uint8_t {
        // Use the scene's active output camera node (set via host command).
        SceneOutput = 0,
        // Host supplies view-projection directly (typical RenderBridge path).
        HostMatrix = 1,
    };

    CameraMode camera_mode = CameraMode::SceneOutput;
    float view_projection[16] = {
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
    };
    glm::vec3 camera_position{0.0f, 0.0f, 0.0f};

    // Legacy wall-clock delta (editor only). Host integrations should prefer production_time_seconds.
    float delta_time = 0.0f;

    // Host-driven deterministic production timing.
    double production_time_seconds = 0.0;
    std::optional<std::uint64_t> production_frame_number;
    bool production_paused = false;
    bool production_discontinuity = false;
    double output_frame_rate_hz = 50.0;
    double simulation_step_seconds = 0.01;
    std::uint64_t scene_seed = 1;
};

} // namespace genesis
