// Reference host integration for PIXERA RenderBridge-style control.
// The host owns the frame loop and drives scene state via HostBridge commands.

#include "host/HostBridge.hpp"
#include "host/HostSceneCommand.hpp"
#include "host/HostTypes.hpp"
#include "platform/Paths.hpp"
#include "scene/ScenePath.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <filesystem>
#include <string>

namespace {

void trySetSceneOutputCamera(genesis::HostBridge& bridge) {
    genesis::HostSceneCommand camera_cmd{};
    camera_cmd.type = genesis::HostSceneCommandType::SetOutputCamera;

    const char* candidates[] = {
        "Demo Scene/Main Camera",
        "Root/SceneCamera",
    };

    for (const char* path : candidates) {
        camera_cmd.path = path;
        if (bridge.applyCommand(camera_cmd)) {
            return;
        }
    }

    // Fall back to first camera node in the scene.
    for (const genesis::Node& node : bridge.sceneRuntime().scene().nodes) {
        if (!node.camera.has_value()) continue;
        camera_cmd.path = genesis::nodePath(bridge.sceneRuntime().scene(), node.id);
        if (bridge.applyCommand(camera_cmd)) {
            return;
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        genesis::HostBridgeConfig config{};
        config.window_title = "GENESIS — Host Bridge (RenderBridge reference)";
        config.enable_stats_overlay = true;

        genesis::HostBridge bridge(config);

        if (argc >= 2) {
            const std::filesystem::path asset = genesis::resolveAssetPath(argv[1]);
            const std::string ext = asset.extension().string();
            genesis::HostSceneCommand load_cmd{};
            if (ext == ".json" || ext == ".scene") {
                load_cmd.type = genesis::HostSceneCommandType::LoadDocument;
            } else {
                load_cmd.type = genesis::HostSceneCommandType::LoadGltf;
            }
            load_cmd.path = asset.string();
            bridge.applyCommand(load_cmd);
        } else {
            bridge.loadDemo();
        }

        trySetSceneOutputCamera(bridge);

        float time = 0.0f;
        while (!bridge.shouldClose()) {
            bridge.pollEvents();

            time += 0.016f;

            // Simulate host-side scene edits arriving over a link.
            if (genesis::Node* sphere = bridge.sceneRuntime().findNodeByPath("Demo Scene/Props/Sphere")) {
                sphere->local.position.y = 0.525f + std::sin(time * 1.5f) * 0.08f;
                bridge.updateTransforms();
            }

            genesis::HostFrameInput frame{};
            frame.camera_mode = genesis::HostFrameInput::CameraMode::SceneOutput;
            frame.delta_time = 0.016f;

            bridge.beginFrame(frame);
            bridge.renderScene();
            bridge.endFrame();
        }

        return 0;
    } catch (const std::exception& ex) {
        spdlog::critical("Host bridge fatal error: {}", ex.what());
        return 1;
    }
}
