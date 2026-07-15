#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rxext::genesis_ext {

struct CatalogActor {
    std::string path;
    std::string group_label;
    std::string param_id;
    std::array<double, 3> default_position{0.0, 0.0, 0.0};
    std::array<double, 3> default_rotation{0.0, 0.0, 0.0};
    std::array<double, 3> default_scale{1.0, 1.0, 1.0};
    std::array<double, 4> default_color{1.0, 1.0, 1.0, 1.0};
    bool default_visible = true;
};

struct CatalogLight {
    std::string path;
    std::string group_label;
    std::string param_id;
    double default_intensity = 1.0;
};

struct SceneParameterCatalog {
    std::vector<CatalogActor> actors;
    std::vector<CatalogLight> lights;
};

std::string parameterIdFromPath(std::string_view node_path);

SceneParameterCatalog buildSceneParameterCatalog(const std::filesystem::path& module_dir);

} // namespace rxext::genesis_ext
