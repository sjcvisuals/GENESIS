#include "scene/ParamBinding.hpp"

#include "scene/ScenePath.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace genesis {

namespace {

constexpr float kCentimetersPerUnit = 100.0f;

bool setNodeRotationComponent(Node& node, int axis, float degrees) {
    const glm::vec3 euler = glm::degrees(glm::eulerAngles(node.local.rotation));
    glm::vec3 updated = euler;
    updated[axis] = degrees;
    const float x = glm::radians(updated.x);
    const float y = glm::radians(updated.y);
    const float z = glm::radians(updated.z);
    const glm::quat qx = glm::angleAxis(x, glm::vec3(1, 0, 0));
    const glm::quat qy = glm::angleAxis(y, glm::vec3(0, 1, 0));
    const glm::quat qz = glm::angleAxis(z, glm::vec3(0, 0, 1));
    node.local.rotation = glm::normalize(qy * qx * qz);
    return true;
}

} // namespace

bool sceneParamAffectsTransforms(OscParamTarget target) {
    switch (target) {
    case OscParamTarget::NodePositionX:
    case OscParamTarget::NodePositionY:
    case OscParamTarget::NodePositionZ:
    case OscParamTarget::NodeScaleX:
    case OscParamTarget::NodeScaleY:
    case OscParamTarget::NodeScaleZ:
    case OscParamTarget::NodeRotationX:
    case OscParamTarget::NodeRotationY:
    case OscParamTarget::NodeRotationZ:
        return true;
    default:
        return false;
    }
}

bool applySceneParam(SceneRuntime& runtime, OscParamTarget target, float value,
                     const std::string& node_path, std::uint32_t material_id) {
    Node* node = node_path.empty() ? nullptr : runtime.findNodeByPath(node_path);
    SceneMaterial* mat = nullptr;
    if (material_id < runtime.materials().size()) {
        mat = runtime.material(material_id);
    }

    switch (target) {
    case OscParamTarget::NodePositionX:
        if (!node) return false;
        node->local.position.x = value / kCentimetersPerUnit;
        return true;
    case OscParamTarget::NodePositionY:
        if (!node) return false;
        node->local.position.y = value / kCentimetersPerUnit;
        return true;
    case OscParamTarget::NodePositionZ:
        if (!node) return false;
        node->local.position.z = value / kCentimetersPerUnit;
        return true;
    case OscParamTarget::NodeScaleX:
        if (!node) return false;
        node->local.scale.x = value / 100.0f;
        return true;
    case OscParamTarget::NodeScaleY:
        if (!node) return false;
        node->local.scale.y = value / 100.0f;
        return true;
    case OscParamTarget::NodeScaleZ:
        if (!node) return false;
        node->local.scale.z = value / 100.0f;
        return true;
    case OscParamTarget::NodeRotationX:
        return node ? setNodeRotationComponent(*node, 0, value) : false;
    case OscParamTarget::NodeRotationY:
        return node ? setNodeRotationComponent(*node, 1, value) : false;
    case OscParamTarget::NodeRotationZ:
        return node ? setNodeRotationComponent(*node, 2, value) : false;
    case OscParamTarget::NodeVisible:
        if (!node) return false;
        node->visible = value > 0.5f;
        return true;
    case OscParamTarget::MaterialBaseColorR:
        if (!mat) return false;
        mat->base_color.r = value;
        return true;
    case OscParamTarget::MaterialBaseColorG:
        if (!mat) return false;
        mat->base_color.g = value;
        return true;
    case OscParamTarget::MaterialBaseColorB:
        if (!mat) return false;
        mat->base_color.b = value;
        return true;
    case OscParamTarget::MaterialBaseColorA:
        if (!mat) return false;
        mat->base_color.a = value;
        return true;
    case OscParamTarget::MaterialMetallic:
        if (!mat) return false;
        mat->metallic = value;
        return true;
    case OscParamTarget::MaterialRoughness:
        if (!mat) return false;
        mat->roughness = value;
        return true;
    case OscParamTarget::MaterialEmissiveStrength:
        if (!mat) return false;
        mat->emissive_strength = value;
        return true;
    case OscParamTarget::LightEnabled:
        if (!node || !node->light.has_value()) return false;
        node->light->enabled = value > 0.5f;
        return true;
    case OscParamTarget::LightIntensity:
        if (!node || !node->light.has_value()) return false;
        node->light->intensity = value;
        return true;
    case OscParamTarget::LightColorR:
        if (!node || !node->light.has_value()) return false;
        node->light->color.r = value;
        return true;
    case OscParamTarget::LightColorG:
        if (!node || !node->light.has_value()) return false;
        node->light->color.g = value;
        return true;
    case OscParamTarget::LightColorB:
        if (!node || !node->light.has_value()) return false;
        node->light->color.b = value;
        return true;
    case OscParamTarget::ParticleRate:
        if (!node || !node->particle_emitter.has_value()) return false;
        node->particle_emitter->rate_per_second = std::max(value, 0.0f);
        return true;
    case OscParamTarget::CameraFov:
        if (!node || !node->camera.has_value()) return false;
        node->camera->fov_y_radians = glm::radians(std::clamp(value, 1.0f, 170.0f));
        return true;
    case OscParamTarget::FluidSourceDensity:
        if (!node || !node->particle_fluid.has_value()) return false;
        node->particle_fluid->source_density = std::max(value, 0.0f);
        return true;
    case OscParamTarget::FluidSourceRadius:
        if (!node || !node->particle_fluid.has_value()) return false;
        node->particle_fluid->source_radius = std::clamp(value, 0.001f, 0.5f);
        return true;
    case OscParamTarget::FluidSourceVelocityY:
        if (!node || !node->particle_fluid.has_value()) return false;
        node->particle_fluid->source_velocity.y = value;
        return true;
    case OscParamTarget::FluidSource2Density:
        if (!node || !node->particle_fluid.has_value()) return false;
        node->particle_fluid->source2_density = std::max(value, 0.0f);
        return true;
    case OscParamTarget::FluidSource2VelocityY:
        if (!node || !node->particle_fluid.has_value()) return false;
        node->particle_fluid->source2_velocity.y = value;
        return true;
    case OscParamTarget::FluidDensityDissipation:
        if (!node || !node->particle_fluid.has_value()) return false;
        node->particle_fluid->density_dissipation = std::clamp(value, 0.9f, 1.0f);
        return true;
    case OscParamTarget::FluidDyeColorR:
        if (!node || !node->particle_fluid.has_value()) return false;
        node->particle_fluid->dye_color.r = std::clamp(value, 0.0f, 1.0f);
        return true;
    case OscParamTarget::FluidDyeColorG:
        if (!node || !node->particle_fluid.has_value()) return false;
        node->particle_fluid->dye_color.g = std::clamp(value, 0.0f, 1.0f);
        return true;
    case OscParamTarget::FluidDyeColorB:
        if (!node || !node->particle_fluid.has_value()) return false;
        node->particle_fluid->dye_color.b = std::clamp(value, 0.0f, 1.0f);
        return true;
    default:
        return false;
    }
}

} // namespace genesis
