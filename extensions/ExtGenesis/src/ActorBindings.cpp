#include "ActorBindings.h"

#include "Input.h"
#include "SceneSelectors.h"

#include "scene/SceneRuntime.hpp"

#include <cctype>
#include <format>
#include <glm/gtc/quaternion.hpp>

namespace rxext::genesis_ext {

namespace {

constexpr const char* kSceneGroupName = "Scene";
constexpr const char* kGroupExpandedProperty = "group_expanded";
constexpr double kColorChannelMax = 255.0;

void applySceneGroupProperties(Parameter* parameter) {
    if (!parameter) {
        return;
    }
    parameter->set_property(PropertyNames::group_name, kSceneGroupName);
    parameter->set_property(kGroupExpandedProperty, string{"true"});
}

std::array<double, 4> colorToUi(const std::array<double, 4>& normalized) {
    return {
        normalized[0] * kColorChannelMax,
        normalized[1] * kColorChannelMax,
        normalized[2] * kColorChannelMax,
        normalized[3] * kColorChannelMax,
    };
}

glm::vec4 colorFromUi(const std::array<double, 4>& ui_color) {
    return glm::vec4(static_cast<float>(ui_color[0] / kColorChannelMax),
                     static_cast<float>(ui_color[1] / kColorChannelMax),
                     static_cast<float>(ui_color[2] / kColorChannelMax),
                     static_cast<float>(ui_color[3] / kColorChannelMax));
}

std::string actorGroup(std::string_view actor_label) {
    return std::string(actor_label);
}

std::string attributeLabel(std::string_view folder, std::string_view component) {
    return std::string(folder) + " / " + std::string(component);
}

glm::quat rotationFromEulerDegrees(const Axis3Binding& rotation) {
    if (!rotation.x || !rotation.y || !rotation.z) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    const glm::vec3 euler_degrees(static_cast<float>(rotation.x->value()),
                                  static_cast<float>(rotation.y->value()),
                                  static_cast<float>(rotation.z->value()));
    return glm::quat(glm::radians(euler_degrees));
}

void applyActorBinding(::genesis::SceneRuntime& runtime, const ActorBinding& binding) {
    if (binding.node_path.empty()) {
        return;
    }

    genesis::Node* node = runtime.findNodeByPath(binding.node_path);
    if (!node || !binding.position.x || !binding.rotation.x || !binding.scale.x || !binding.color ||
        !binding.mesh_enabled) {
        return;
    }

    node->visible = binding.mesh_enabled->value();
    node->local.position = glm::vec3(static_cast<float>(binding.position.x->value()),
                                     static_cast<float>(binding.position.y->value()),
                                     static_cast<float>(binding.position.z->value()));
    node->local.rotation = rotationFromEulerDegrees(binding.rotation);
    node->local.scale = glm::vec3(static_cast<float>(binding.scale.x->value()),
                                  static_cast<float>(binding.scale.y->value()),
                                  static_cast<float>(binding.scale.z->value()));

    const auto& color = binding.color->value();
    if (node->material_resource.has_value()) {
        if (genesis::SceneMaterial* material = runtime.material(*node->material_resource)) {
            material->base_color = colorFromUi(color);
        }
    }
}

} // namespace

ParameterValue* ActorBindings::registerAxis3(Axis3Binding& axes,
                                             Input& input,
                                             const std::string& prefix,
                                             const std::string& actor_group,
                                             std::string_view folder_label,
                                             const std::array<double, 3>& defaults,
                                             double min_ui,
                                             double max_ui) {
    auto add_axis = [&](const char* axis, double default_value) {
        ParameterValue* parameter =
            input.add_parameter<ParameterValue>(prefix + "_" + axis, default_value);
        std::string label = axis;
        if (!label.empty()) {
            label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
        }
        parameter->set_property(PropertyNames::name, attributeLabel(folder_label, label));
        parameter->set_property(PropertyNames::group_name, actor_group);
        parameter->set_property(PropertyNames::min_value_ui, min_ui);
        parameter->set_property(PropertyNames::max_value_ui, max_ui);
        return parameter;
    };

    axes.x = add_axis("x", defaults[0]);
    axes.y = add_axis("y", defaults[1]);
    axes.z = add_axis("z", defaults[2]);
    return axes.x;
}

void ActorBindings::registerSceneParameters(Input& input,
                                              ParameterInt*& show_select,
                                              ParameterBool*& use_scene_camera) {
    show_select = input.add_parameter<ParameterInt>("show_select", 0);
    show_select->set_property(PropertyNames::name, "Show");
    applySceneGroupProperties(show_select);

    use_scene_camera = input.add_parameter<ParameterBool>("use_scene_camera", false);
    use_scene_camera->set_property(PropertyNames::name, "Use Scene Camera");
    applySceneGroupProperties(use_scene_camera);
}

void ActorBindings::registerActor(Input& input, const CatalogActor& actor) {
    ActorBinding binding{};
    binding.group_label = actor.group_label;
    binding.node_path = actor.path;
    const std::string prefix = std::format("actor_{}", actor.param_id);
    const std::string group = actorGroup(binding.group_label);

    registerAxis3(binding.position,
                  input,
                  prefix + "_position",
                  group,
                  "Position",
                  actor.default_position,
                  -1000.0,
                  1000.0);
    registerAxis3(binding.rotation,
                  input,
                  prefix + "_rotation",
                  group,
                  "Rotation",
                  actor.default_rotation,
                  -360.0,
                  360.0);
    registerAxis3(binding.scale,
                  input,
                  prefix + "_scale",
                  group,
                  "Scale",
                  actor.default_scale,
                  0.01,
                  100.0);

    binding.color = input.add_parameter<ParameterVector4>(prefix + "_color",
                                                          colorToUi(actor.default_color));
    binding.color->set_property(PropertyNames::name, "Color");
    binding.color->set_property(PropertyNames::group_name, group);
    binding.color->set_property(PropertyNames::purpose, PurposeNames::Color);
    binding.color->set_property(PropertyNames::min_value, 0.0);
    binding.color->set_property(PropertyNames::max_value, kColorChannelMax);
    binding.color->set_property(PropertyNames::min_value_ui, 0.0);
    binding.color->set_property(PropertyNames::max_value_ui, kColorChannelMax);

    binding.mesh_enabled =
        input.add_parameter<ParameterBool>(prefix + "_mesh_enabled", actor.default_visible);
    binding.mesh_enabled->set_property(PropertyNames::name, "Mesh Enabled");
    binding.mesh_enabled->set_property(PropertyNames::group_name, group);

    actors_.push_back(binding);
}

void ActorBindings::registerActors(Input& input, const SceneParameterCatalog& catalog) {
    actors_.clear();
    lights_.clear();

    actors_.reserve(catalog.actors.size());
    for (const CatalogActor& actor : catalog.actors) {
        registerActor(input, actor);
    }

    lights_.reserve(catalog.lights.size());
    for (const CatalogLight& light : catalog.lights) {
        LightBinding binding{};
        binding.node_path = light.path;
        const std::string prefix = std::format("light_{}", light.param_id);

        binding.intensity = input.add_parameter<ParameterValue>(prefix + "_intensity", light.default_intensity);
        binding.intensity->set_property(PropertyNames::name, "Intensity");
        binding.intensity->set_property(PropertyNames::group_name, actorGroup(light.group_label));
        binding.intensity->set_property(PropertyNames::min_value, 0.0);
        binding.intensity->set_property(PropertyNames::max_value, 100.0);

        lights_.push_back(binding);
    }
}

void ActorBindings::apply(::genesis::SceneRuntime& runtime) const {
    for (const ActorBinding& binding : actors_) {
        applyActorBinding(runtime, binding);
    }

    for (const LightBinding& binding : lights_) {
        genesis::Node* light = runtime.findNodeByPath(binding.node_path);
        if (!light || !binding.intensity || !light->light.has_value()) {
            continue;
        }
        light->light->intensity = static_cast<float>(binding.intensity->value());
        light->light->enabled = true;
    }
}

} // namespace rxext::genesis_ext
