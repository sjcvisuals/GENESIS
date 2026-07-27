#include "runtime/MultiViewSession.hpp"

#include "runtime/RuntimeScene.hpp"
#include "scene/Lighting.hpp"
#include "scene/Scene.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstring>

namespace genesis {

namespace {

double elapsedMs(const std::chrono::steady_clock::time_point& start) {
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

} // namespace

MultiViewSession::MultiViewSession(const ProductionTopology& topology,
                                   const MultiViewSessionConfig& config)
    : topology_(topology), node_assignment_(config.node_assignment) {
    output_assignments_ = resolveOutputAssignments(topology_, node_assignment_);
    if (output_assignments_.empty()) {
        throw std::runtime_error("MultiViewSession: no outputs resolved");
    }

    const OutputTopology& first_output = output_assignments_.front().output;
    OffscreenRendererConfig primary_config{};
    primary_config.adapter_luid = config.adapter_luid;
    primary_config.width = first_output.width;
    primary_config.height = first_output.height;
    primary_config.enable_validation = config.enable_validation;

    outputs_.reserve(output_assignments_.size());
    outputs_.push_back(
        OutputSlot{output_assignments_.front(), std::make_unique<OffscreenRenderer>(primary_config)});

    VulkanContext& shared_context = const_cast<VulkanContext&>(outputs_.front().renderer->context());
    scene_ = std::make_unique<SceneRuntime>(shared_context);

    ProductionClockConfig clock_config{};
    clock_config.output_frame_rate_hz = topology_.logical_canvas.frame_rate_hz;
    production_clock_.setConfig(clock_config);

    for (std::size_t i = 1; i < output_assignments_.size(); ++i) {
        const OutputTopology& output = output_assignments_[i].output;
        outputs_.push_back(OutputSlot{
            output_assignments_[i],
            std::make_unique<OffscreenRenderer>(shared_context, output.width, output.height),
        });
    }

    // Transparent clear for media-server compositing; shared cinematic look defaults.
    look_settings_.display = DisplayColorSpace::Rec709;
    applyLookSettings();

    spdlog::info("MultiViewSession: {} outputs on {} / {}", outputs_.size(), node_assignment_.machine_id,
                 node_assignment_.gpu_id);
}

MultiViewSession::~MultiViewSession() = default;

void MultiViewSession::applyLookSettings() {
    color_mgmt_.settings() = look_settings_;
    const ColorLut3D& lut = color_mgmt_.rebuildDisplayLut();
    for (OutputSlot& slot : outputs_) {
        slot.renderer->setClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        slot.renderer->uploadDisplayLut(lut);
    }
}

void MultiViewSession::loadResolved(const RuntimeSceneResource& resource) {
    if (!tryLoadResolved(resource)) {
        throw std::runtime_error(last_load_error_.empty() ? "Scene load failed" : last_load_error_);
    }
}

bool MultiViewSession::tryLoadResolved(const RuntimeSceneResource& resource) {
    auto candidate = std::make_unique<SceneRuntime>(outputs_.front().renderer->context());
    if (!tryLoadRuntimeScene(*candidate, resource, &last_load_error_)) {
        spdlog::error("MultiViewSession scene load failed: {}", last_load_error_);
        return false;
    }

    scene_ = std::move(candidate);
    last_load_error_.clear();
    return true;
}

void MultiViewSession::updateTransforms() {
    scene_->updateTransforms();
}

GenesisFrameTime MultiViewSession::advanceProductionTime(const HostTimingInput& input) {
    current_frame_time_ = production_clock_.advance(input);
    return current_frame_time_;
}

void MultiViewSession::prepareFrameUniforms(const OutputSlot& slot, const glm::dmat4& view,
                                            const glm::dmat4& projection,
                                            FrameUniforms& out_uniforms) const {
    (void)slot;
    const glm::dmat4 view_proj = projection * view;
    glm::mat4 view_proj_f = glm::mat4(view_proj);

    glm::vec3 camera_pos{0.0f, 0.0f, 0.0f};
    if (std::optional<glm::vec3> pos = scene_->outputCameraPosition()) {
        camera_pos = *pos;
    }

    out_uniforms = buildFrameUniforms(scene_->scene(), glm::value_ptr(view_proj_f), camera_pos,
                                      &look_settings_,
                                      static_cast<float>(current_frame_time_.production_time_seconds));
}

MultiViewFrameMetrics MultiViewSession::renderAllOutputs(const glm::dmat4& host_view,
                                                         const glm::dmat4& host_projection,
                                                         const bool use_scene_camera) {
    const auto frame_start = std::chrono::steady_clock::now();

    const auto update_start = std::chrono::steady_clock::now();
    updateTransforms();
    const double update_ms = elapsedMs(update_start);

    MultiViewFrameMetrics metrics{};
    metrics.shared_scene_update_ms = update_ms;
    metrics.output_count = static_cast<std::uint32_t>(outputs_.size());
    metrics.outputs.reserve(outputs_.size());

    double total_render_ms = 0.0;

    for (OutputSlot& slot : outputs_) {
        const OutputTopology& output = slot.assignment.output;
        const float aspect = (output.height > 0) ? static_cast<float>(output.width) /
                                                       static_cast<float>(output.height)
                                                 : 1.0f;

        glm::dmat4 render_view = host_view;
        glm::dmat4 render_projection = host_projection;

        if (use_scene_camera) {
            if (const Node* cam_node = scene_->scene().activeOutputCameraNode();
                cam_node && cam_node->camera.has_value()) {
                const glm::mat4 view_f = cam_node->camera->viewMatrix(cam_node->world);
                const glm::mat4 proj_f = cam_node->camera->projectionMatrix(aspect);
                render_view = glm::dmat4(view_f);
                render_projection = glm::dmat4(proj_f);
            }
        }

        slot.renderer->resize(output.width, output.height);

        FrameUniforms frame_uniforms{};
        prepareFrameUniforms(slot, render_view, render_projection, frame_uniforms);

        const auto render_start = std::chrono::steady_clock::now();

        slot.renderer->setFrameUniforms(frame_uniforms);
        slot.renderer->beginFrame();

        SceneRenderStats render_stats{};
        SceneDrawCollectParams collect_params{};
        collect_params.view_proj_col_major = frame_uniforms.view_proj;
        collect_params.enable_cpu_culling = true;
        collect_params.enable_gpu_culling = false;
        scene_->collectDrawCommands(draw_commands_, &render_stats, collect_params);
        buildDrawBatches(draw_commands_, draw_batches_, &render_stats);

        MeshDrawOptions draw_options{};
        draw_options.view_proj_col_major = frame_uniforms.view_proj;
        draw_options.enable_gpu_cull = false;
        slot.renderer->drawMeshBatches(draw_batches_, draw_options);
        slot.renderer->endFrame();
        slot.renderer->waitFrameComplete();

        const double render_ms = elapsedMs(render_start);
        total_render_ms += render_ms;

        OutputRenderMetrics output_metrics{};
        output_metrics.output_id = output.output_id;
        output_metrics.width = output.width;
        output_metrics.height = output.height;
        output_metrics.render_ms = render_ms;
        output_metrics.draw_calls = render_stats.draw_calls;
        output_metrics.triangles = render_stats.triangles;
        output_metrics.visible_mesh_nodes = render_stats.visible_mesh_nodes;
        metrics.outputs.push_back(output_metrics);
    }

    metrics.total_output_render_ms = total_render_ms;
    metrics.wall_frame_ms = elapsedMs(frame_start);
    return metrics;
}

} // namespace genesis
