#include "scene/Lighting.hpp"

#include "color/ColorManagement.hpp"
#include "scene/SceneVisibility.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>

namespace genesis {

namespace {

glm::vec3 worldPosition(const glm::mat4& world) {
    return glm::vec3(world[3]);
}

glm::vec3 worldForward(const glm::mat4& world) {
    return glm::normalize(glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
}

void writeLight(GpuLight& out, const SceneLight& light, const glm::mat4& world) {
    out.params[0] = static_cast<float>(static_cast<int>(light.type));
    out.params[1] = light.range;
    out.params[2] = std::cos(light.inner_cone_radians);
    out.params[3] = std::cos(light.outer_cone_radians);

    const glm::vec3 pos = worldPosition(world);
    const glm::vec3 dir = worldForward(world);

    out.position[0] = pos.x;
    out.position[1] = pos.y;
    out.position[2] = pos.z;
    out.position[3] = 0.0f;

    out.direction[0] = dir.x;
    out.direction[1] = dir.y;
    out.direction[2] = dir.z;
    out.direction[3] = 0.0f;

    out.color[0] = light.color.r;
    out.color[1] = light.color.g;
    out.color[2] = light.color.b;
    out.color[3] = light.intensity;
}

void writeVec4(float* out, float x, float y, float z, float w) {
    out[0] = x;
    out[1] = y;
    out[2] = z;
    out[3] = w;
}

} // namespace

FrameUniforms buildFrameUniforms(const Scene& scene, const float* view_proj_col_major,
                                 const glm::vec3& camera_position,
                                 const ColorManagementSettings* color, float time_seconds,
                                 bool game_output) {
    FrameUniforms u{};
    std::memcpy(u.view_proj, view_proj_col_major, sizeof(u.view_proj));
    u.camera_pos[0] = camera_position.x;
    u.camera_pos[1] = camera_position.y;
    u.camera_pos[2] = camera_position.z;
    u.camera_pos[3] = color ? std::max(color->exposure, 0.01f) : 1.0f;

    // fog_color.a carries film-grain strength. Default 0 — grain reads as fireflies on soft additive.
    // Ambient/fog filled below based on whether the scene has analytic lights (main Remover path).
    const float grain = color ? std::clamp(color->grain_strength, 0.0f, 0.2f) : 0.0f;

    u.display_params[0] = color ? static_cast<float>(static_cast<int>(color->display)) : 0.0f;
    u.display_params[1] = color ? std::clamp(color->bloom_strength, 0.0f, 2.0f) : 0.18f;
    u.display_params[2] = color ? std::clamp(color->vignette_strength, 0.0f, 1.0f) : 0.22f;
    u.display_params[3] = time_seconds;

    int count = 0;
    for (const Node& node : scene.nodes) {
        if (!node.light.has_value() || !node.light->enabled) continue;
        if (!isNodeDrawnInView(scene, node, game_output)) continue;
        if (count >= 8) break;
        writeLight(u.lights[count], *node.light, node.world);
        ++count;
    }
    u.light_count = count;

    if (count == 0) {
        // No analytic lights → no residual studio ambient / fog fill.
        u.ibl_intensity = 0.0f;
        writeVec4(u.ambient_sky, 0.0f, 0.0f, 0.0f, 1.0f);
        writeVec4(u.ambient_ground, 0.0f, 0.0f, 0.0f, 1.0f);
        u.fog_density = 0.0f;
        u.fog_height = 0.0f;
        writeVec4(u.fog_color, 0.0f, 0.0f, 0.0f, grain);
    } else {
        // Modest hemisphere fill only when the scene actually has lights.
        u.ibl_intensity = 0.55f;
        writeVec4(u.ambient_sky, 0.08f, 0.10f, 0.14f, 1.0f);
        writeVec4(u.ambient_ground, 0.028f, 0.024f, 0.020f, 1.0f);
        u.fog_density = 0.0018f;
        u.fog_height = 0.04f;
        writeVec4(u.fog_color, 0.10f, 0.11f, 0.13f, grain);
    }
    return u;
}

} // namespace genesis
