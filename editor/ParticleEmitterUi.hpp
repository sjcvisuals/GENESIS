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
    // Live-apply emitter authoring to living particles (trail width, enable, …).
    std::function<void()> apply_particles;
    std::function<void(NodeId)> select_node;
    std::function<ParticleWorldStatsLite()> stats;
    // Optional: per-emitter alive + measured spawn rate (particles/sec).
    std::function<EmitterLiveUiStats(NodeId)> emitter_live;
};

// True when this empty/parent node is a Tugela Particle System (flag and/or emitters).
// Remains true with zero emitters so Add Emitter still works in Tugela.
bool isParticleSystemNode(const Scene& scene, const Node& node);

// Outliner suffix: "SYS" / empty. Emitters are not Hierarchy actors.
const char* particleHierarchyBadge(const Scene& scene, const Node& node);

// Hierarchy/outliner: emitters live under Systems but are authored only in Tugela.
bool isHierarchyHiddenParticleEmitter(const Node& node);

// Tugela System inspector (emitter list + add templates).
void drawParticleSystemInspector(Node& system_node, ParticleEmitterUiContext& ctx);

// Tugela Emitter module stack.
void drawParticleEmitterInspector(Node& emitter_node, const std::string& node_path,
                                  ParticleEmitterUiContext& ctx);

} // namespace genesis
