#pragma once

#include "SceneParameterCatalog.h"
#include "scene/GnsPackage.hpp"
#include "rxext_client.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace genesis {
class Node;
class SceneRuntime;
}

namespace rxext::genesis_ext {

class Input;

struct Axis3Binding {
    ParameterValue* x = nullptr;
    ParameterValue* y = nullptr;
    ParameterValue* z = nullptr;
};

struct ActorBinding {
    std::string group_label;
    std::string node_path;
    Axis3Binding position;
    Axis3Binding rotation;
    Axis3Binding scale;
    ParameterVector4* color = nullptr;
    ParameterBool* mesh_enabled = nullptr;
};

struct LightBinding {
    std::string node_path;
    ParameterValue* intensity = nullptr;
};

class ActorBindings {
public:
    void registerSceneParameters(Input& input,
                                 ParameterInt*& show_select,
                                 ParameterBool*& use_scene_camera,
                                 ParameterBool*& reload_scene,
                                 ParameterVector3*& target_position);
    void registerActors(Input& input, const SceneParameterCatalog& catalog);
    std::size_t registerNewFromCatalog(Input& input, const SceneParameterCatalog& catalog);
    void applyGnsBindings(const genesis::GnsBindings& bindings, genesis::SceneRuntime& runtime);
    void syncAllFromScene(genesis::SceneRuntime& runtime) const;
    void apply(genesis::SceneRuntime& runtime) const;

    std::size_t actorCount() const { return actors_.size(); }
    std::size_t lightCount() const { return lights_.size(); }
    const std::string& primaryTargetNodePath() const { return primary_target_node_path_; }
    const std::string& primaryLightNodePath() const { return primary_light_node_path_; }

private:
    bool hasActorPath(const std::string& node_path) const;
    bool hasLightPath(const std::string& node_path) const;
    void registerActor(Input& input, const CatalogActor& actor);
    void registerLight(Input& input, const CatalogLight& light);
    void syncBindingFromScene(genesis::SceneRuntime& runtime, const ActorBinding& binding,
                              const genesis::Node& node) const;
    void applyActorBinding(genesis::SceneRuntime& runtime, const ActorBinding& binding) const;
    void applyPrimaryTargetBinding(genesis::SceneRuntime& runtime, const ActorBinding& binding) const;

    ParameterValue* registerAxis3(Axis3Binding& axes,
                                  Input& input,
                                  const std::string& prefix,
                                  const std::string& actor_group,
                                  std::string_view folder_label,
                                  const std::array<double, 3>& defaults,
                                  double min_ui,
                                  double max_ui);

    std::vector<ActorBinding> actors_;
    std::vector<LightBinding> lights_;
    ParameterVector3* target_position_ = nullptr;
    std::string primary_target_node_path_;
    std::string primary_light_node_path_;
};

} // namespace rxext::genesis_ext
