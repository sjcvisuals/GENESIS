#include "runtime/PerfDebug.hpp"

#include "engine/FrameStats.hpp"
#include "particles/ParticleWorld.hpp"

namespace genesis {

void fillPerfDebugFromFrameStats(PerfDebugSnapshot& snap, const FrameStats& stats) {
    snap.frame_ms = stats.frame_ms;
    snap.update_ms = stats.update_ms;
    snap.imgui_ms = stats.imgui_ms;
    snap.draw_ms = stats.draw_ms;
    snap.draw_calls = stats.draw_calls;
    snap.triangles = stats.triangles;
    snap.visible_mesh_nodes = stats.visible_mesh_nodes;
    snap.scene_nodes = stats.scene_nodes;
    snap.active_lights = stats.active_lights;
}

void fillPerfDebugParticles(PerfDebugSnapshot& snap, const ParticleWorld& world) {
    const ParticleSimStats ps = world.stats();
    snap.particles_alive = ps.alive;
    snap.particle_emitters = ps.emitters;
    snap.particle_steps = ps.steps;
    snap.particle_gpu = world.gpuComputeEnabled();
}

} // namespace genesis
