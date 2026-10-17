#include "runtime/GenesisSession.hpp"

#include "runtime/RuntimeScene.hpp"
#include "runtime/ScenePlaybackPipeline.hpp"
#include "renderer/vulkan/ExternalSync.hpp"
#include "scene/Lighting.hpp"
#include "scene/Scene.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>

namespace genesis {

GenesisSession::GenesisSession(const OffscreenRendererConfig& config) {
    renderer_ = std::make_unique<OffscreenRenderer>(config);
    scene_ = std::make_unique<SceneRuntime>(renderer_->context());
    width_ = config.width;
    height_ = config.height;
    // PIXERA composites over clips — keep target clear transparent.
    renderer_->setClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    // PIXERA-friendly Rec.709 display by default; cinematic bloom/vignette/grain still apply.
    look_settings_.display = DisplayColorSpace::Rec709;
    applyLookSettings();
}

GenesisSession::~GenesisSession() = default;

void GenesisSession::applyLookSettings() {
    color_mgmt_.settings() = look_settings_;
    renderer_->uploadDisplayLut(color_mgmt_.rebuildDisplayLut());
}

void GenesisSession::resize(std::uint32_t width, std::uint32_t height) {
    width_ = width;
    height_ = height;
    renderer_->resize(width, height);
}

void GenesisSession::loadResolved(const RuntimeSceneResource& resource) {
    if (!tryLoadResolved(resource)) {
        throw std::runtime_error(last_load_error_.empty() ? "Scene load failed" : last_load_error_);
    }
}

bool GenesisSession::tryLoadResolved(const RuntimeSceneResource& resource) {
    auto candidate = std::make_unique<SceneRuntime>(renderer_->context());
    std::optional<GnsPixeraConfig> pixera;

    if (!tryLoadRuntimeScene(*candidate, resource, &last_load_error_)) {
        spdlog::error("GenesisSession scene load failed: {}", last_load_error_);
        return false;
    }

    if (resource.kind == RuntimeSceneKind::Gns) {
        try {
            pixera = GnsPackage::load(resource.path).pixera;
        } catch (const std::exception& ex) {
            last_load_error_ = ex.what();
            spdlog::error("GenesisSession GNS metadata load failed: {}", last_load_error_);
            return false;
        }
    }

    scene_ = std::move(candidate);
    last_pixera_config_ = pixera;
    last_load_error_.clear();
    return true;
}

void GenesisSession::updateTransforms() {
    scene_->updateTransforms();
}

GenesisFrameTime GenesisSession::advanceProductionTime(const HostTimingInput& input) {
    current_frame_time_ = production_clock_.advance(input);
    return current_frame_time_;
}

void GenesisSession::tickFrame(const GenesisFrameTime& frame_time) {
    tickScenePlayback(*scene_, frame_time);
}

void GenesisSession::prepareFrameUniforms(const glm::dmat4& view, const glm::dmat4& projection,
                                          const GenesisFrameTime& frame_time) {
    const glm::dmat4 view_proj = projection * view;
    glm::mat4 view_proj_f = glm::mat4(view_proj);

    glm::vec3 camera_pos{0.0f, 0.0f, 0.0f};
    if (std::optional<glm::vec3> pos = scene_->outputCameraPosition()) {
        camera_pos = *pos;
    }

    frame_uniforms_ = buildFrameUniforms(scene_->scene(), glm::value_ptr(view_proj_f), camera_pos,
                                         &look_settings_,
                                         static_cast<float>(frame_time.production_time_seconds));
}

void GenesisSession::render(const glm::dmat4& view,
                            const glm::dmat4& projection,
                            const GenesisFrameTime& frame_time,
                            bool use_scene_camera) {
    current_frame_time_ = frame_time;

    glm::dmat4 render_view = view;
    glm::dmat4 render_projection = projection;

    if (use_scene_camera) {
        const float aspect =
            (height_ > 0) ? static_cast<float>(width_) / static_cast<float>(height_) : 1.0f;
        if (const Node* cam_node = scene_->scene().activeOutputCameraNode();
            cam_node && cam_node->camera.has_value()) {
            const glm::mat4 view_f = cam_node->camera->viewMatrix(cam_node->world);
            const glm::mat4 proj_f = cam_node->camera->projectionMatrix(aspect);
            render_view = glm::dmat4(view_f);
            render_projection = glm::dmat4(proj_f);
        }
    }

    prepareFrameUniforms(render_view, render_projection, frame_time);
    renderer_->setFrameUniforms(frame_uniforms_);
    renderer_->beginFrame();

    const float fluid_dt =
        frame_time.delta_time_seconds > 0.0 ? static_cast<float>(frame_time.delta_time_seconds) : 0.0f;
    if (renderer_->canDispatchCompute()) {
        scene_->simulateFluids(renderer_->currentCommandBuffer(), fluid_dt);
    }

    draw_commands_.clear();
    SceneRenderStats render_stats{};
    SceneDrawCollectParams collect_params{};
    collect_params.view_proj_col_major = frame_uniforms_.view_proj;
    collect_params.enable_cpu_culling = true;
    collect_params.enable_gpu_culling = false;
    {
        const glm::mat3 inv = glm::inverse(glm::mat3(glm::mat4(render_view)));
        scene_->setParticleViewBasis(glm::normalize(glm::vec3(inv[0])),
                                     glm::normalize(glm::vec3(inv[1])));
    }
    collectSceneDrawCommands(*scene_, draw_commands_, &render_stats, collect_params);
    buildDrawBatches(draw_commands_, draw_batches_, &render_stats);

    MeshDrawOptions draw_options{};
    draw_options.view_proj_col_major = frame_uniforms_.view_proj;
    draw_options.enable_gpu_cull = false;
    draw_options.enable_depth_prepass = true;
    renderer_->drawMeshBatches(draw_batches_, draw_options);

    renderer_->endFrame(renderer_->sync().hostFinishedSemaphore());
}

} // namespace genesis
