#include "scene/Lighting.hpp"

#include "color/ColorManagement.hpp"

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
                                 const ColorManagementSettings* color) {
    FrameUniforms u{};
    std::memcpy(u.view_proj, view_proj_col_major, sizeof(u.view_proj));
    u.camera_pos[0] = camera_position.x;
    u.camera_pos[1] = camera_position.y;
    u.camera_pos[2] = camera_position.z;
    u.camera_pos[3] = color ? std::max(color->exposure, 0.01f) : 1.15f;

    u.ibl_intensity = 0.55f;
    writeVec4(u.ambient_sky, 0.42f, 0.52f, 0.72f, 1.0f);
    writeVec4(u.ambient_ground, 0.18f, 0.14f, 0.10f, 1.0f);

    u.fog_density = 0.012f;
    u.fog_height = 0.08f;
    writeVec4(u.fog_color, 0.55f, 0.62f, 0.72f, 1.0f);

    u.display_params[0] = color ? static_cast<float>(static_cast<int>(color->display)) : 0.0f;

    int count = 0;
    for (const Node& node : scene.nodes) {
        if (!node.visible || !node.light.has_value() || !node.light->enabled) continue;
        if (count >= 8) break;
        writeLight(u.lights[count], *node.light, node.world);
        ++count;
    }
    u.light_count = count;
    return u;
}

} // namespace genesis
