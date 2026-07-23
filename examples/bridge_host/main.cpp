// Reference host integration for PIXERA RenderBridge-style control.
// The host owns the frame loop and drives scene state via HostBridge commands.

#include "host/HostBridge.hpp"
#include "host/HostSceneCommand.hpp"
#include "host/HostTypes.hpp"
#include "platform/Paths.hpp"
#include "runtime/RuntimePaths.hpp"
#include "scene/ScenePath.hpp"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>

namespace {

std::filesystem::path moduleDirectory() {
    if (const char* base = SDL_GetBasePath()) {
        return std::filesystem::path(base);
    }
    return {};
}

bool isPackageExtension(const std::filesystem::path& path) {
    const std::string ext = path.extension().string();
    return ext == ".gns" || ext == ".GNS" || ext == ".genesis" || ext == ".GENESIS";
}

bool isDocumentExtension(const std::filesystem::path& path) {
    const std::string ext = path.extension().string();
    return ext == ".json" || ext == ".JSON" || ext == ".scene" || ext == ".SCENE";
}

void trySetSceneOutputCamera(genesis::HostBridge& bridge) {
    genesis::HostSceneCommand camera_cmd{};
    camera_cmd.type = genesis::HostSceneCommandType::SetOutputCamera;

    for (const genesis::Node& node : bridge.sceneRuntime().scene().nodes) {
        if (!node.camera.has_value()) continue;
        camera_cmd.path = genesis::nodePath(bridge.sceneRuntime().scene(), node.id);
        if (bridge.applyCommand(camera_cmd)) {
            return;
        }
    }
}

void fillHostLookAtViewProjection(genesis::HostFrameInput& frame, const float aspect) {
    const glm::vec3 eye(8.0f, 4.5f, 9.0f);
    const glm::vec3 target(0.0f, 0.0f, 0.0f);
    const glm::mat4 view = glm::lookAtRH(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(55.0f), aspect, 0.1f, 200.0f);
    // Match Vulkan clip space Y flip used by the editor/player cameras.
    glm::mat4 vulkan_proj = proj;
    vulkan_proj[1][1] *= -1.0f;
    const glm::mat4 view_proj = vulkan_proj * view;
    std::memcpy(frame.view_projection, &view_proj[0][0], sizeof(float) * 16);
    frame.camera_position = eye;
    frame.camera_mode = genesis::HostFrameInput::CameraMode::HostMatrix;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path module_dir = moduleDirectory();
        genesis::setRuntimeAssetRoots(module_dir);

        genesis::HostBridgeConfig config{};
        config.window_title = "GENESIS — Host Bridge (RenderBridge reference)";
        config.enable_stats_overlay = true;

        genesis::HostBridge bridge(config);

        if (argc >= 2) {
            const std::filesystem::path asset = genesis::resolveAssetPath(argv[1]);
            genesis::HostSceneCommand load_cmd{};
            if (isPackageExtension(asset)) {
                load_cmd.type = genesis::HostSceneCommandType::LoadGns;
            } else if (isDocumentExtension(asset)) {
                load_cmd.type = genesis::HostSceneCommandType::LoadDocument;
            } else {
                load_cmd.type = genesis::HostSceneCommandType::LoadGltf;
            }
            load_cmd.path = asset.string();

            std::string load_error;
            if (!bridge.applyCommand(load_cmd, &load_error)) {
                spdlog::critical("Failed to load '{}': {}", asset.string(), load_error);
                return 1;
            }
            spdlog::info("Host bridge loaded '{}'", asset.string());
        } else {
            bridge.loadDemo();
            spdlog::info("Host bridge loaded demo scene");
        }

        trySetSceneOutputCamera(bridge);

        double production_time_seconds = 0.0;
        std::uint64_t production_frame_number = 0;

        while (!bridge.shouldClose()) {
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
                fillHostLookAtViewProjection(frame, aspect);
            }

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
