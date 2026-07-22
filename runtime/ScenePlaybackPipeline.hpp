#pragma once

#include "runtime/GenesisFrameTime.hpp"
#include "scene/SceneRuntime.hpp"

#include <vector>

#include <volk.h>

namespace genesis {

// Advances scene simulation for production / player hosts (PIXERA timeline, HostBridge).
void tickScenePlayback(SceneRuntime& scene, const GenesisFrameTime& frame_time);

void simulateSceneFluids(VkCommandBuffer cmd, SceneRuntime& scene, float dt_seconds);

void collectSceneDrawCommands(SceneRuntime& scene, std::vector<SceneDrawCommand>& out,
                              SceneRenderStats* out_stats,
                              const SceneDrawCollectParams& params);

} // namespace genesis
