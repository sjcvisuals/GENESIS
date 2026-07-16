#pragma once

#include "animation/AnimationClip.hpp"
#include "scene/Scene.hpp"

namespace genesis {

// Samples a clip at `time_seconds` and writes TRS into matching scene nodes by name.
void applyAnimationClip(Scene& scene, const AnimationClip& clip, float time_seconds, bool loop);

void tickAnimationPlayback(AnimationPlayback& playback, const AnimationClip* clip, float dt_seconds);

} // namespace genesis
