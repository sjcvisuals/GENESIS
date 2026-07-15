#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
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
    float delta_time = 0.0f;
};

} // namespace genesis
