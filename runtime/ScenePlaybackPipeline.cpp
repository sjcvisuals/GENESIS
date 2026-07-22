#include "runtime/ScenePlaybackPipeline.hpp"

#include "animation/AnimationEvaluator.hpp"

namespace genesis {

namespace {

void syncAnimationToProductionTime(SceneRuntime& scene, double production_time_seconds) {
    AnimationPlayback& playback = scene.animationPlayback();
    if (playback.clip_index < 0 ||
        playback.clip_index >= static_cast<std::int32_t>(scene.animationClips().size())) {
        return;
    }

    const AnimationClip& clip =
        scene.animationClips()[static_cast<std::size_t>(playback.clip_index)];
    playback.time_seconds = static_cast<float>(production_time_seconds);
    applyAnimationClip(scene.scene(), clip, playback.time_seconds, playback.loop);
}

} // namespace

void tickScenePlayback(SceneRuntime& scene, const GenesisFrameTime& frame_time) {
    scene.particleWorld().setSceneSeed(frame_time.scene_seed);

    if (frame_time.is_discontinuity) {
        scene.resetParticles();
    }

    if (!scene.animationClips().empty()) {
        AnimationPlayback& playback = scene.animationPlayback();
        if (playback.clip_index < 0) {
            playback.clip_index = 0;
        }
        syncAnimationToProductionTime(scene, frame_time.production_time_seconds);
    }

    if (!frame_time.is_paused) {
        if (frame_time.simulation_steps_this_frame > 0 && frame_time.fixed_step_seconds > 0.0) {
            scene.tickParticlesSteps(frame_time.simulation_steps_this_frame,
                                     static_cast<float>(frame_time.fixed_step_seconds));
        } else if (frame_time.delta_time_seconds > 0.0) {
            scene.tickParticles(static_cast<float>(frame_time.delta_time_seconds),
                                static_cast<float>(frame_time.fixed_step_seconds));
        }
    }

    scene.updateTransforms();
}

void simulateSceneFluids(VkCommandBuffer cmd, SceneRuntime& scene, const float dt_seconds) {
    scene.simulateFluids(cmd, dt_seconds);
}

void collectSceneDrawCommands(SceneRuntime& scene, std::vector<SceneDrawCommand>& out,
                              SceneRenderStats* out_stats, const SceneDrawCollectParams& params) {
    scene.collectDrawCommands(out, out_stats, params);
    scene.appendFluidDrawCommands(out);
    scene.appendParticleDrawCommands(out);

    if (!out_stats) {
        return;
    }

    out_stats->draw_calls = static_cast<std::uint32_t>(out.size());
    out_stats->triangles = 0;
    for (const SceneDrawCommand& command : out) {
        if (command.mesh) {
            out_stats->triangles += command.mesh->index_count / 3;
        }
    }
}

} // namespace genesis
