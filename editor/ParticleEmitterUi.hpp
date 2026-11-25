#pragma once

#include "editor/AudioReactiveUi.hpp"
#include "editor/OscProtocolUi.hpp"
#include "particles/ParticleEmitter.hpp"
#include "scene/Scene.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace genesis {

struct ParticleWorldStatsLite {
    std::uint32_t alive = 0;
    std::uint32_t emitters = 0;
    std::uint32_t steps = 0;
};

struct EmitterLiveUiStats {
    std::uint32_t alive = 0;
    float measured_spawn_rate = 0.0f;
    float authored_rate = 0.0f;
    bool found = false;
};

struct ParticleEmitterUiContext {
    Scene* scene = nullptr;
    OscLinkUiContext* osc = nullptr;
    AudioLinkUiContext* audio = nullptr;
    std::function<void()> mark_dirty;
    std::function<void()> reset_particles;
    std::function<void(NodeId)> select_node;
    std::function<ParticleWorldStatsLite()> stats;
    // Optional: per-emitter alive + measured spawn rate (particles/sec).
    std::function<EmitterLiveUiStats(NodeId)> emitter_live;
};

// True when this empty/parent node owns one or more emitter children (Tugela System).
bool isParticleSystemNode(const Scene& scene, const Node& node);

// Outliner suffix: "SYS" / "EM" / empty.
const char* particleHierarchyBadge(const Scene& scene, const Node& node);

// Tugela System inspector (emitter list + add templates).
void drawParticleSystemInspector(Node& system_node, ParticleEmitterUiContext& ctx);

// Tugela Emitter module stack.
void drawParticleEmitterInspector(Node& emitter_node, const std::string& node_path,
                                  ParticleEmitterUiContext& ctx);

} // namespace genesis
