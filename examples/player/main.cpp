// Minimal Genesis Player — validates the same scene loading path as the RenderBridge extension.
// Uses windowed HostBridge for display; scene resolution uses runtime/RuntimePaths.

#include "host/HostBridge.hpp"
#include "runtime/RuntimePaths.hpp"
#include "scene/ScenePath.hpp"

#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <iostream>
#include <string>

namespace {

std::filesystem::path playerModuleDirectory() {
    if (const char* base = SDL_GetBasePath()) {
        const std::filesystem::path dir = base;
        SDL_free(const_cast<char*>(base));
        return dir;
    }
    return {};
}

void trySetSceneOutputCamera(genesis::HostBridge& bridge) {
    for (const genesis::Node& node : bridge.sceneRuntime().scene().nodes) {
        if (!node.camera.has_value()) {
            continue;
        }
        genesis::HostSceneCommand camera_cmd{};
        camera_cmd.type = genesis::HostSceneCommandType::SetOutputCamera;
        camera_cmd.path = genesis::nodePath(bridge.sceneRuntime().scene(), node.id);
        if (bridge.applyCommand(camera_cmd)) {
            return;
        }
    }
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

        trySetSceneOutputCamera(bridge);
        spdlog::info("Genesis Player loaded '{}'", resource);

        double production_time_seconds = 0.0;
        std::uint64_t production_frame_number = 0;

        while (!bridge.shouldClose()) {
            bridge.pollEvents();

            production_time_seconds += 1.0 / 50.0;
            ++production_frame_number;

            genesis::HostFrameInput frame{};
            frame.camera_mode = genesis::HostFrameInput::CameraMode::SceneOutput;
            frame.production_time_seconds = production_time_seconds;
            frame.production_frame_number = production_frame_number;

            bridge.beginFrame(frame);
            bridge.renderScene();
            bridge.endFrame();
        }

        genesis::clearRuntimeAssetRoots();
        return 0;
    } catch (const std::exception& ex) {
        spdlog::critical("Genesis Player fatal error: {}", ex.what());
        return 1;
    }
}
