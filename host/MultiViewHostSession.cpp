#include "host/MultiViewHostSession.hpp"

#include <algorithm>
#include <chrono>
#include <spdlog/spdlog.h>

namespace genesis {

namespace {

double elapsedMs(const std::chrono::steady_clock::time_point& start) {
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

void presentSizeForOutput(const OutputTopology& output, const std::uint32_t max_w, const std::uint32_t max_h,
                          std::uint32_t& out_w, std::uint32_t& out_h) {
    out_w = output.width;
    out_h = output.height;
    if (out_w == 0 || out_h == 0) {
        out_w = max_w;
        out_h = max_h;
        return;
    }
    const double scale =
        std::min(1.0, std::min(static_cast<double>(max_w) / out_w, static_cast<double>(max_h) / out_h));
    out_w = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(out_w * scale));
    out_h = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(out_h * scale));
}

} // namespace

MultiViewHostSession::MultiViewHostSession(const ProductionTopology& topology,
                                           const MultiViewHostConfig& config)
    : topology_(topology), node_assignment_(config.node_assignment) {
    output_assignments_ = resolveOutputAssignments(topology_, node_assignment_);

    const OutputTopology& first_output = output_assignments_.front().output;
    std::uint32_t present_w = 0;
    std::uint32_t present_h = 0;
    presentSizeForOutput(first_output, config.max_present_width, config.max_present_height, present_w,
                         present_h);

    HostBridgeConfig bridge_config{};
    bridge_config.window_title = config.window_title;
    bridge_config.width = static_cast<int>(present_w);
    bridge_config.height = static_cast<int>(present_h);
    bridge_config.enable_stats_overlay = config.enable_stats_overlay;

    bridge_ = std::make_unique<HostBridge>(bridge_config);
    max_present_width_ = config.max_present_width;
    max_present_height_ = config.max_present_height;

    ProductionClockConfig clock_config{};
    clock_config.output_frame_rate_hz = topology_.logical_canvas.frame_rate_hz;
    production_clock_.setConfig(clock_config);

    spdlog::info("MultiViewHostSession: {} outputs on {} / {} (present up to {}x{})",
                 output_assignments_.size(), node_assignment_.machine_id, node_assignment_.gpu_id,
                 max_present_width_, max_present_height_);
}

MultiViewHostSession::~MultiViewHostSession() = default;

void MultiViewHostSession::loadResolved(const RuntimeSceneResource& resource) {
    if (!tryLoadResolved(resource)) {
        throw std::runtime_error(last_load_error_.empty() ? "Scene load failed" : last_load_error_);
    }
}

bool MultiViewHostSession::tryLoadResolved(const RuntimeSceneResource& resource) {
    if (!bridge_->tryLoadRuntimeResource(resource, &last_load_error_)) {
        spdlog::error("MultiViewHostSession scene load failed: {}", last_load_error_);
        return false;
    }
    last_load_error_.clear();
    return true;
}

GenesisFrameTime MultiViewHostSession::advanceProductionTime(const HostTimingInput& input) {
    current_frame_time_ = production_clock_.advance(input);
    return current_frame_time_;
}

MultiViewFrameMetrics MultiViewHostSession::renderAllOutputs(const bool use_scene_camera) {
    const auto frame_start = std::chrono::steady_clock::now();

    const auto update_start = std::chrono::steady_clock::now();
    bridge_->updateTransforms();
    const double update_ms = elapsedMs(update_start);

    MultiViewFrameMetrics metrics{};
    metrics.shared_scene_update_ms = update_ms;
    metrics.output_count = static_cast<std::uint32_t>(output_assignments_.size());
    metrics.outputs.reserve(output_assignments_.size());

    double total_render_ms = 0.0;

    for (const ResolvedOutputAssignment& assignment : output_assignments_) {
        const OutputTopology& output = assignment.output;
        std::uint32_t present_w = 0;
        std::uint32_t present_h = 0;
        presentSizeForOutput(output, max_present_width_, max_present_height_, present_w, present_h);
        // Avoid swapchain thrash: only resize when present size actually changes.
        if (static_cast<int>(present_w) != bridge_->width() ||
            static_cast<int>(present_h) != bridge_->height()) {
            spdlog::debug("Resizing present target to {}x{} for {}", present_w, present_h, output.output_id);
            bridge_->resize(static_cast<int>(present_w), static_cast<int>(present_h));
        }

        HostFrameInput frame{};
        frame.camera_mode = use_scene_camera ? HostFrameInput::CameraMode::SceneOutput
                                             : HostFrameInput::CameraMode::HostMatrix;
        frame.production_time_seconds = current_frame_time_.production_time_seconds;
        frame.production_frame_number = current_frame_time_.production_frame_number;
        frame.output_frame_rate_hz = topology_.logical_canvas.frame_rate_hz;

        const auto render_start = std::chrono::steady_clock::now();
        bridge_->beginFrame(frame);
        bridge_->renderScene();
        bridge_->endFrame();
        const double render_ms = elapsedMs(render_start);
        total_render_ms += render_ms;

        const FrameStats& stats = bridge_->frameStats();
        OutputRenderMetrics output_metrics{};
        output_metrics.output_id = output.output_id;
        output_metrics.width = output.width;
        output_metrics.height = output.height;
        output_metrics.render_ms = render_ms;
        output_metrics.draw_calls = stats.draw_calls;
        output_metrics.triangles = stats.triangles;
        output_metrics.visible_mesh_nodes = stats.visible_mesh_nodes;
        metrics.outputs.push_back(output_metrics);
    }

    metrics.total_output_render_ms = total_render_ms;
    metrics.wall_frame_ms = elapsedMs(frame_start);
    return metrics;
}

} // namespace genesis
