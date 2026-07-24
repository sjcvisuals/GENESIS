#pragma once

#include "renderer/FrameUniforms.hpp"
#include "scene/Scene.hpp"

#include <glm/vec3.hpp>

namespace genesis {

struct ColorManagementSettings;

FrameUniforms buildFrameUniforms(const Scene& scene, const float* view_proj_col_major,
                                 const glm::vec3& camera_position,
                                 const ColorManagementSettings* color = nullptr,
                                 float time_seconds = 0.0f);

} // namespace genesis
