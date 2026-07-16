#include "runtime/SceneStateHash.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <sstream>

namespace genesis {

namespace {

std::uint64_t fnv1a64(const void* data, std::size_t size) {
    std::uint64_t hash = 14695981039346656037ull;
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= 1099511628211ull;
    }
    return hash;
}

void hashFloat(std::uint64_t& hash, float value) {
    hash = fnv1a64(&value, sizeof(value)) ^ (hash * 1099511628211ull);
}

void hashVec3(std::uint64_t& hash, const glm::vec3& value) {
    hashFloat(hash, value.x);
    hashFloat(hash, value.y);
    hashFloat(hash, value.z);
}

void hashQuat(std::uint64_t& hash, const glm::quat& value) {
    hashFloat(hash, value.w);
    hashFloat(hash, value.x);
    hashFloat(hash, value.y);
    hashFloat(hash, value.z);
}

void hashMat4(std::uint64_t& hash, const glm::mat4& value) {
    hash = fnv1a64(glm::value_ptr(value), sizeof(float) * 16) ^ (hash * 1099511628211ull);
}

} // namespace

std::string sceneStateContentHash(const Scene& scene) {
    std::uint64_t hash = 14695981039346656037ull;

    if (scene.active_output_camera.has_value()) {
        const std::uint64_t camera_id = *scene.active_output_camera;
        hash = fnv1a64(&camera_id, sizeof(camera_id)) ^ (hash * 1099511628211ull);
    }

    for (const Node& node : scene.nodes) {
        hashFloat(hash, static_cast<float>(node.id));
        hashVec3(hash, node.local.position);
        hashQuat(hash, node.local.rotation);
        hashVec3(hash, node.local.scale);
        hashMat4(hash, node.world);

        const std::uint8_t visible = node.visible ? 1 : 0;
        hash = fnv1a64(&visible, sizeof(visible)) ^ (hash * 1099511628211ull);

        if (node.camera.has_value()) {
            hashFloat(hash, node.camera->fov_y_radians);
            hashFloat(hash, node.camera->near_plane);
            hashFloat(hash, node.camera->far_plane);
        }
        if (node.light.has_value()) {
            hashFloat(hash, node.light->intensity);
            const std::uint8_t enabled = node.light->enabled ? 1 : 0;
            hash = fnv1a64(&enabled, sizeof(enabled)) ^ (hash * 1099511628211ull);
        }
    }

    std::ostringstream out;
    out << std::hex << hash;
    return out.str();
}

} // namespace genesis
