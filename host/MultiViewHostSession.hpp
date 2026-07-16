#pragma once

#include "host/HostBridge.hpp"
#include "runtime/GenesisFrameTime.hpp"
#include "runtime/NodeAssignment.hpp"
#include "runtime/OutputRenderMetrics.hpp"
#include "runtime/ProductionTopology.hpp"
#include "runtime/RuntimeScene.hpp"

#include <memory>
#include <string>
#include <vector>

namespace genesis {

struct MultiViewHostConfig {
    std::string window_title = "GENESIS Multi-View";
    NodeAssignment node_assignment{};
    bool enable_stats_overlay = false;
    // Cap presentation size so local multi-view does not open 4K windows.
    std::uint32_t max_present_width = 1280;
    std::uint32_t max_present_height = 720;
};

// Windowed multi-view runner for the Player / PIXERA playback path.
// One shared scene, sequential per-output renders. Not used by the editor (single monitor).
class MultiViewHostSession {
public:
    MultiViewHostSession(const ProductionTopology& topology, const MultiViewHostConfig& config);
    ~MultiViewHostSession();

    void loadResolved(const RuntimeSceneResource& resource);
    bool tryLoadResolved(const RuntimeSceneResource& resource);

    GenesisFrameTime advanceProductionTime(const HostTimingInput& input);
    MultiViewFrameMetrics renderAllOutputs(bool use_scene_camera = true);

    HostBridge& bridge() { return *bridge_; }
    const ProductionTopology& topology() const { return topology_; }
    const std::vector<ResolvedOutputAssignment>& outputAssignments() const { return output_assignments_; }
    const std::string& lastLoadError() const { return last_load_error_; }

private:
    ProductionTopology topology_;
    NodeAssignment node_assignment_;
    std::vector<ResolvedOutputAssignment> output_assignments_;
    std::unique_ptr<HostBridge> bridge_;
    ProductionClock production_clock_{};
    GenesisFrameTime current_frame_time_{};
    std::string last_load_error_;
    std::uint32_t max_present_width_ = 1280;
    std::uint32_t max_present_height_ = 720;
};

} // namespace genesis
