#include "host/HostBridge.hpp"

#include "platform/Paths.hpp"
#include "platform/Window.hpp"
#include "renderer/Renderer.hpp"
#include "runtime/HostTimingAdapter.hpp"
#include "runtime/ScenePlaybackPipeline.hpp"
#include "host/HostFrameTiming.hpp"
#include "runtime/RuntimeScene.hpp"
#include "scene/Lighting.hpp"
#include "scene/ScenePath.hpp"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <stdexcept>

namespace genesis {

namespace {

void setError(std::string* out_error, const std::string& message) {
    if (out_error) *out_error = message;
}

} // namespace

HostBridge::HostBridge(const HostBridgeConfig& config) {
    WindowConfig window_config{};
    window_config.title = config.window_title;
    window_config.width = config.width;
    window_config.height = config.height;
    window_config.resizable = config.resizable;

    window_ = std::make_unique<Window>(window_config);
    renderer_ = std::make_unique<Renderer>(*window_);
    scene_ = std::make_unique<SceneRuntime>(renderer_->context());
    stats_overlay_ = config.enable_stats_overlay;
    // Windowed player: studio backdrop + cinematic look defaults (same as editor).
    renderer_->setClearColor(0.035f, 0.038f, 0.045f, 1.0f);
    look_settings_.display = DisplayColorSpace::Rec709;
    applyLookSettings();
}

HostBridge::~HostBridge() {
    draw_commands_.clear();
    draw_batches_.clear();
    // Scene GPU resources must be destroyed while the Renderer Vulkan device still exists.
    scene_.reset();
    renderer_.reset();
    window_.reset();
}

void HostBridge::applyLookSettings() {
    color_mgmt_.settings() = look_settings_;
    renderer_->uploadDisplayLut(color_mgmt_.rebuildDisplayLut());
}

bool HostBridge::shouldClose() const {
    return window_->shouldClose();
}

void HostBridge::pollEvents() {
    window_->pollEvents();
}

void HostBridge::resize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    if (width == window_->width() && height == window_->height()) {
        return;
    }
    window_->handleResize(width, height);
    renderer_->onResize(width, height);
}

int HostBridge::width() const {
    return window_->width();
}

int HostBridge::height() const {
    return window_->height();
}

const char* HostBridge::deviceName() const {
    return renderer_->deviceName();
}

void HostBridge::loadDemo() {
    scene_->loadDemo();
    scene_->updateTransforms();
}

void HostBridge::loadGltf(const std::filesystem::path& path) {
    scene_->loadGltf(path);
    scene_->updateTransforms();
}

void HostBridge::loadDocument(const std::filesystem::path& path) {
    scene_->loadDocument(path);
    scene_->updateTransforms();
}

void HostBridge::loadGns(const std::filesystem::path& path) {
    scene_->loadGnsFile(path);
    scene_->updateTransforms();
}

bool HostBridge::tryLoadRuntimeResource(const RuntimeSceneResource& resource, std::string* out_error) {
    return tryLoadRuntimeScene(*scene_, resource, out_error);
}

void HostBridge::saveDocument(const std::filesystem::path& path) {
    scene_->saveDocument(path);
}

bool HostBridge::applyCommand(const HostSceneCommand& command, std::string* out_error) {
    try {
        switch (command.type) {
        case HostSceneCommandType::LoadDemo:
            loadDemo();
            return true;

        case HostSceneCommandType::LoadGltf:
            loadGltf(resolveAssetPath(command.path));
            return true;

        case HostSceneCommandType::LoadDocument:
            loadDocument(resolveAssetPath(command.path));
            return true;

        case HostSceneCommandType::LoadGns:
            loadGns(resolveAssetPath(command.path));
            return true;

        case HostSceneCommandType::SaveDocument:
            saveDocument(resolveAssetPath(command.path));
            return true;

        case HostSceneCommandType::SetOutputCamera: {
            if (std::optional<NodeId> id = findNodeByPath(scene_->scene(), command.path)) {
                scene_->scene().setActiveOutputCamera(*id);
                return true;
            }
            setError(out_error, "Output camera node not found: " + command.path);
            return false;
        }

        case HostSceneCommandType::ClearOutputCamera:
            scene_->scene().active_output_camera.reset();
            return true;

        case HostSceneCommandType::SetNodeTransform: {
            if (!command.transform.has_value()) {
                setError(out_error, "SetNodeTransform requires transform");
                return false;
            }
            Node* node = scene_->findNodeByPath(command.path);
            if (!node) {
                setError(out_error, "Node not found: " + command.path);
                return false;
            }
            node->local = *command.transform;
            scene_->updateTransforms();
            return true;
        }

        case HostSceneCommandType::SetNodeVisible: {
            if (!command.visible.has_value()) {
                setError(out_error, "SetNodeVisible requires visible flag");
                return false;
            }
            Node* node = scene_->findNodeByPath(command.path);
            if (!node) {
                setError(out_error, "Node not found: " + command.path);
                return false;
            }
            node->visible = *command.visible;
            return true;
        }

        case HostSceneCommandType::SetMaterialBaseColor: {
            if (!command.base_color.has_value()) {
                setError(out_error, "SetMaterialBaseColor requires base_color");
                return false;
            }
            const std::uint32_t mat_id = scene_->resolveMaterialRef(command.path);
            if (SceneMaterial* mat = scene_->material(mat_id)) {
                mat->base_color = *command.base_color;
                return true;
            }
            setError(out_error, "Material not found: " + command.path);
            return false;
        }

        case HostSceneCommandType::SetMaterialMetallicRoughness: {
            const std::uint32_t mat_id = scene_->resolveMaterialRef(command.path);
            SceneMaterial* mat = scene_->material(mat_id);
            if (!mat) {
                setError(out_error, "Material not found: " + command.path);
                return false;
            }
            if (command.metallic.has_value()) mat->metallic = *command.metallic;
            if (command.roughness.has_value()) mat->roughness = *command.roughness;
            return true;
        }

        case HostSceneCommandType::SetLightEnabled: {
            if (!command.light_enabled.has_value()) {
                setError(out_error, "SetLightEnabled requires light_enabled");
                return false;
            }
            Node* node = scene_->findNodeByPath(command.path);
            if (!node || !node->light.has_value()) {
                setError(out_error, "Light node not found: " + command.path);
                return false;
            }
            node->light->enabled = *command.light_enabled;
            return true;
        }

        case HostSceneCommandType::SetLightIntensity: {
            if (!command.light_intensity.has_value()) {
                setError(out_error, "SetLightIntensity requires light_intensity");
                return false;
            }
            Node* node = scene_->findNodeByPath(command.path);
            if (!node || !node->light.has_value()) {
                setError(out_error, "Light node not found: " + command.path);
                return false;
            }
            node->light->intensity = *command.light_intensity;
            return true;
        }
        }
    } catch (const std::exception& ex) {
        setError(out_error, ex.what());
        return false;
    }

    setError(out_error, "Unknown host scene command");
    return false;
}

void HostBridge::updateTransforms() {
    scene_->updateTransforms();
}

void HostBridge::prepareFrameUniforms(const HostFrameInput& input) {
    const float aspect = (height() > 0) ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;
    glm::mat4 view_proj{1.0f};
    glm::vec3 camera_pos = input.camera_position;

    if (input.camera_mode == HostFrameInput::CameraMode::SceneOutput) {
        if (std::optional<glm::mat4> vp = scene_->outputCameraViewProjection(aspect)) {
            view_proj = *vp;
        }
        if (std::optional<glm::vec3> pos = scene_->outputCameraPosition()) {
            camera_pos = *pos;
        }
    } else {
        std::memcpy(&view_proj[0][0], input.view_projection, sizeof(float) * 16);
    }

    frame_uniforms_ = buildFrameUniforms(scene_->scene(), &view_proj[0][0], camera_pos,
                                         &look_settings_,
                                         static_cast<float>(current_frame_time_.production_time_seconds));
}

void HostBridge::beginFrame(const HostFrameInput& input) {
    if (window_->wasResized()) {
        resize(window_->width(), window_->height());
        window_->clearResizedFlag();
    }

    current_frame_time_ = adaptHostFrameInput(production_clock_, input);
    tickScenePlayback(*scene_, current_frame_time_);

    prepareFrameUniforms(input);
    renderer_->setFrameUniforms(frame_uniforms_);
    renderer_->beginFrame();
}

void HostBridge::renderScene() {
    const float fluid_dt = current_frame_time_.delta_time_seconds > 0.0
                               ? static_cast<float>(current_frame_time_.delta_time_seconds)
                               : 0.0f;
    if (renderer_->canDispatchCompute()) {
        scene_->simulateFluids(renderer_->currentCommandBuffer(), fluid_dt);
    }

    SceneRenderStats render_stats{};
    SceneDrawCollectParams collect_params{};
    collect_params.view_proj_col_major = frame_uniforms_.view_proj;
    collect_params.enable_cpu_culling = true;
    collect_params.enable_gpu_culling = false;
    collectSceneDrawCommands(*scene_, draw_commands_, &render_stats, collect_params);

    frame_stats_.draw_calls = render_stats.draw_calls;
    frame_stats_.triangles = render_stats.triangles;
    frame_stats_.visible_mesh_nodes = render_stats.visible_mesh_nodes;
    frame_stats_.scene_nodes = render_stats.scene_nodes;
    frame_stats_.active_lights = render_stats.active_lights;

    buildDrawBatches(draw_commands_, draw_batches_, &render_stats);

    MeshDrawOptions draw_options{};
    draw_options.view_proj_col_major = frame_uniforms_.view_proj;
    draw_options.enable_gpu_cull = false;
    draw_options.enable_depth_prepass = true;
    renderer_->drawMeshBatches(draw_batches_, draw_options);
}

void HostBridge::endFrame() {
    renderer_->beginUiFrame();
    if (stats_overlay_) {
        drawStatsOverlay();
    }
    renderer_->endFrame();
}

void HostBridge::drawStatsOverlay() {
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::Begin("HostBridge", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("GPU: %s", deviceName());
    ImGui::Text("Draw calls: %u", frame_stats_.draw_calls);
    ImGui::Text("Triangles: %u", frame_stats_.triangles);
    ImGui::Text("Nodes: %u", frame_stats_.scene_nodes);
    ImGui::Text("Lights: %u", frame_stats_.active_lights);
    ImGui::End();
}

} // namespace genesis
