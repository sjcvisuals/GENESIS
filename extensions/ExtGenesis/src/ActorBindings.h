#pragma once

#include "SceneParameterCatalog.h"
#include "rxext_client.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace genesis {
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
                                 ParameterBool*& use_scene_camera);
    void registerActors(Input& input, const SceneParameterCatalog& catalog);
    void apply(::genesis::SceneRuntime& runtime) const;

    std::size_t actorCount() const { return actors_.size(); }
    std::size_t lightCount() const { return lights_.size(); }

private:
    void registerActor(Input& input, const CatalogActor& actor);

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
};

} // namespace rxext::genesis_ext
