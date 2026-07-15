#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include "scene/Scene.hpp"
#include "scene/Transform.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cmath>
#include <cstdint>
#include <optional>

namespace genesis {

// 1 world unit = 1 meter. Display uses centimeters (100 cm per meter).
constexpr float kCentimetersPerUnit = 100.0f;

struct ViewportRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 1.0f;
    float h = 1.0f;
    bool hovered = false;

    float aspect() const { return (h > 0.0f) ? w / h : 1.0f; }

    bool contains(float px, float py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }

    float localMouseX(float screen_x) const { return screen_x - x; }
    float localMouseY(float screen_y) const { return screen_y - y; }
};

inline void mat4ToFloat16(const glm::mat4& m, float out[16]) {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            out[col * 4 + row] = m[col][row];
        }
    }
}

inline glm::mat4 float16ToMat4(const float m[16]) {
    glm::mat4 out(1.0f);
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            out[col][row] = m[col * 4 + row];
        }
    }
    return out;
}

// ImGuizmo expects OpenGL-style clip Y; undo the Vulkan flip applied to render projections.
inline glm::mat4 projectionForImGuizmo(glm::mat4 projection) {
    projection[1][1] *= -1.0f;
    return projection;
}

inline glm::quat rotationLookAt(const glm::vec3& eye, const glm::vec3& target,
                                const glm::vec3& world_up = glm::vec3(0.0f, 1.0f, 0.0f)) {
    const glm::vec3 forward = glm::normalize(target - eye);
    if (glm::length(forward) < 1e-5f) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    const glm::vec3 right = glm::normalize(glm::cross(world_up, forward));
    const glm::vec3 up = glm::cross(forward, right);
    const glm::mat3 basis(right, up, -forward);
    return glm::quat_cast(basis);
}

inline glm::quat cameraRotationFromLookAt(const glm::vec3& eye, const glm::vec3& target,
                                          const glm::vec3& world_up = glm::vec3(0.0f, 1.0f, 0.0f)) {
    const glm::mat4 view = glm::lookAtRH(eye, target, world_up);
    const glm::mat4 world = glm::inverse(view);
    glm::vec3 scale{};
    glm::vec3 skew{};
    glm::vec4 perspective{};
    glm::quat rotation{};
    glm::vec3 position{};
    if (!glm::decompose(world, scale, rotation, position, skew, perspective)) {
        return rotationLookAt(eye, target, world_up);
    }
    return glm::normalize(rotation);
}

inline void extractYawPitchFromForward(const glm::vec3& forward, float& yaw, float& pitch) {
    yaw = std::atan2(forward.x, -forward.z);
    pitch = std::asin(glm::clamp(forward.y, -1.0f, 1.0f));
}

inline bool decomposeTransform(const glm::mat4& matrix, Transform& out) {
    glm::vec3 skew;
    glm::vec4 perspective;
    return glm::decompose(matrix, out.scale, out.rotation, out.position, skew, perspective);
}

inline glm::mat4 parentWorldMatrix(const Scene& scene, const Node& node) {
    if (!node.parent.has_value()) {
        return glm::mat4(1.0f);
    }
    if (const Node* parent = scene.find(*node.parent)) {
        return parent->world;
    }
    return glm::mat4(1.0f);
}

inline glm::mat4 worldToLocalMatrix(const Scene& scene, const Node& node, const glm::mat4& world) {
    return glm::inverse(parentWorldMatrix(scene, node)) * world;
}

struct ScreenRay {
    glm::vec3 origin;
    glm::vec3 direction;
};

inline ScreenRay rayFromScreen(const glm::mat4& view, const glm::mat4& projection, float aspect,
                               float mouse_x, float mouse_y, float viewport_w, float viewport_h) {
    const float ndc_x = (2.0f * mouse_x / viewport_w) - 1.0f;
    const float ndc_y = 1.0f - (2.0f * mouse_y / viewport_h);

    const glm::mat4 inv_vp = glm::inverse(projection * view);
    const glm::vec4 near_point = inv_vp * glm::vec4(ndc_x, ndc_y, 0.0f, 1.0f);
    const glm::vec4 far_point = inv_vp * glm::vec4(ndc_x, ndc_y, 1.0f, 1.0f);

    const glm::vec3 near_world = glm::vec3(near_point) / near_point.w;
    const glm::vec3 far_world = glm::vec3(far_point) / far_point.w;

    ScreenRay ray{};
    ray.origin = near_world;
    ray.direction = glm::normalize(far_world - near_world);
    return ray;
}

} // namespace genesis
