#pragma once

#include "runtime/GenesisFrameTime.hpp"
#include "runtime/NodeAssignment.hpp"
#include "runtime/OutputRenderMetrics.hpp"
#include "runtime/ProductionTopology.hpp"
#include "runtime/RuntimeScene.hpp"
#include "renderer/OffscreenRenderer.hpp"
#include "scene/SceneDrawBatch.hpp"
#include "scene/SceneRuntime.hpp"

#include <glm/mat4x4.hpp>
#include <memory>
#include <string>
#include <vector>

namespace genesis {

struct MultiViewSessionConfig {
    std::uint64_t adapter_luid = 0;
    NodeAssignment node_assignment{};
    bool enable_validation = false;
};

// One shared scene + production clock, N offscreen outputs from topology assignment.
class MultiViewSession {
public:
    MultiViewSession(const ProductionTopology& topology, const MultiViewSessionConfig& config);
    ~MultiViewSession();

    MultiViewSession(const MultiViewSession&) = delete;
    MultiViewSession& operator=(const MultiViewSession&) = delete;

    void loadResolved(const RuntimeSceneResource& resource);
    bool tryLoadResolved(const RuntimeSceneResource& resource);

    void updateTransforms();

    GenesisFrameTime advanceProductionTime(const HostTimingInput& input);
    const GenesisFrameTime& currentFrameTime() const { return current_frame_time_; }

    MultiViewFrameMetrics renderAllOutputs(const glm::dmat4& host_view,
                                           const glm::dmat4& host_projection,
                                           bool use_scene_camera);

    const ProductionTopology& topology() const { return topology_; }
    const NodeAssignment& nodeAssignment() const { return node_assignment_; }
    const std::vector<ResolvedOutputAssignment>& outputAssignments() const { return output_assignments_; }
    SceneRuntime& sceneRuntime() { return *scene_; }
    const SceneRuntime& sceneRuntime() const { return *scene_; }
    const std::string& lastLoadError() const { return last_load_error_; }

private:
    struct OutputSlot {
        ResolvedOutputAssignment assignment;
        std::unique_ptr<OffscreenRenderer> renderer;
    };

    void prepareFrameUniforms(const OutputSlot& slot, const glm::dmat4& view, const glm::dmat4& projection,
                              FrameUniforms& out_uniforms) const;

    ProductionTopology topology_;
    NodeAssignment node_assignment_;
    std::vector<ResolvedOutputAssignment> output_assignments_;
    std::vector<OutputSlot> outputs_;
    std::unique_ptr<SceneRuntime> scene_;
    std::vector<SceneDrawCommand> draw_commands_;
    std::vector<SceneMeshBatch> draw_batches_;
    ProductionClock production_clock_{};
    GenesisFrameTime current_frame_time_{};
    std::string last_load_error_;
};

} // namespace genesis
