#pragma once

#include "protocol/OscTypes.hpp"
#include "scene/SceneRuntime.hpp"

#include <cstdint>
#include <string>

namespace genesis {

// Shared OSC / audio-reactive parameter write path.
bool applySceneParam(SceneRuntime& runtime, OscParamTarget target, float value,
                     const std::string& node_path, std::uint32_t material_id = 0);

// True when applying the target may dirty node transforms.
bool sceneParamAffectsTransforms(OscParamTarget target);

} // namespace genesis
