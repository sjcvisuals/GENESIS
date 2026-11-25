// Minimal Genesis Player — validates the same scene loading path as the RenderBridge extension.
// Uses windowed HostBridge for display; scene resolution uses runtime/RuntimePaths.

#include "host/HostBridge.hpp"
#include "platform/FrameLimiter.hpp"
#include "runtime/RuntimePaths.hpp"
#include "scene/ScenePath.hpp"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>

#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

std::filesystem::path playerModuleDirectory() {
    if (const char* base = SDL_GetBasePath()) {
        // SDL3: GetBasePath is owned by SDL — do not SDL_free.
        return std::filesystem::path(base);
    }
    return {};
}

bool trySetSceneOutputCamera(genesis::HostBridge& bridge) {
    for (const genesis::Node& node : bridge.sceneRuntime().scene().nodes) {
        if (!node.camera.has_value()) {
            continue;
        }
        genesis::HostSceneCommand camera_cmd{};
        camera_cmd.type = genesis::HostSceneCommandType::SetOutputCamera;
        camera_cmd.path = genesis::nodePath(bridge.sceneRuntime().scene(), node.id);
        if (bridge.applyCommand(camera_cmd)) {
            return true;
        }
    }
    return false;
}

void fillFallbackLookAtViewProjection(genesis::HostFrameInput& frame, const float aspect) {
    const glm::vec3 eye(8.0f, 4.5f, 9.0f);
    const glm::vec3 target(0.0f, 0.0f, 0.0f);
    const glm::mat4 view = glm::lookAtRH(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(55.0f), aspect, 0.1f, 200.0f);
    glm::mat4 vulkan_proj = proj;
    vulkan_proj[1][1] *= -1.0f;
    const glm::mat4 view_proj = vulkan_proj * view;
    std::memcpy(frame.view_projection, &view_proj[0][0], sizeof(float) * 16);
    frame.camera_position = eye;
    frame.camera_mode = genesis::HostFrameInput::CameraMode::HostMatrix;
}

void printUsage() {
    std::cout << "Genesis Player — runtime scene verifier\n"
              << "Usage: genesis_player [scene_resource]\n"
              << "  scene_resource: .gns, .scene.json, .glb, demo (default)\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path module_dir = playerModuleDirectory();
        genesis::setRuntimeAssetRoots(module_dir);

        std::string resource = "demo";
        if (argc >= 2) {
            if (std::string(argv[1]) == "--help") {
                printUsage();
                return 0;
            }
            resource = argv[1];
        }

        genesis::HostBridgeConfig config{};
        config.window_title = "GENESIS Player";
        config.enable_stats_overlay = true;

        genesis::HostBridge bridge(config);

        const std::optional<genesis::RuntimeSceneResource> resolved =
            genesis::resolveRuntimeSceneResource(module_dir, resource);
        if (!resolved.has_value()) {
            spdlog::error("Scene resource not found: {}", resource);
            return 1;
        }

        std::string load_error;
        if (!bridge.tryLoadRuntimeResource(*resolved, &load_error)) {
            spdlog::error("Failed to load scene '{}': {}", resource, load_error);
            return 1;
        }

        const bool has_output_camera = trySetSceneOutputCamera(bridge);
        spdlog::info("Genesis Player loaded '{}' (output camera: {})", resource,
                     has_output_camera ? "yes" : "fallback look-at");

        // Match editor default: cap at 50 FPS so particle / production timing stays steady.
        genesis::FrameLimiter frame_limiter{};
        frame_limiter.setEnabled(true);
        frame_limiter.setTargetFps(50.0f);
        spdlog::info("Player frame limiter ON @ 50 FPS");

        double production_time_seconds = 0.0;
        std::uint64_t production_frame_number = 0;

        while (!bridge.shouldClose()) {
            frame_limiter.beginFrame();
            bridge.pollEvents();

            production_time_seconds += 1.0 / 50.0;
            ++production_frame_number;

            genesis::HostFrameInput frame{};
            frame.production_time_seconds = production_time_seconds;
            frame.production_frame_number = production_frame_number;

            if (bridge.sceneRuntime().scene().active_output_camera.has_value()) {
                frame.camera_mode = genesis::HostFrameInput::CameraMode::SceneOutput;
            } else {
                const float aspect =
                    bridge.height() > 0
                        ? static_cast<float>(bridge.width()) / static_cast<float>(bridge.height())
                        : 1.0f;
                fillFallbackLookAtViewProjection(frame, aspect);
            }

            bridge.beginFrame(frame);
            bridge.renderScene();
            bridge.endFrame();
            frame_limiter.endFrame();
        }

        genesis::clearRuntimeAssetRoots();
        return 0;
    } catch (const std::exception& ex) {
        spdlog::critical("Genesis Player fatal error: {}", ex.what());
        return 1;
    }
}
