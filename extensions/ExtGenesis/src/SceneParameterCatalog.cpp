#include "SceneParameterCatalog.h"

#include "SceneSelectors.h"
#include "scene/GnsPackage.hpp"
#include "scene/SceneDocument.hpp"
#include "scene/SceneSerializer.hpp"

#include <cmath>
#include <map>
#include <stdexcept>

#include <glm/gtc/quaternion.hpp>

namespace rxext::genesis_ext {

namespace {

std::string friendlyNodeLabel(const std::string& node_path) {
    const std::size_t slash = node_path.find_last_of('/');
    if (slash == std::string::npos) {
        return node_path;
    }
    return node_path.substr(slash + 1);
}

std::optional<genesis::SceneDocument> loadDocumentForShow(const std::filesystem::path& module_dir,
                                                          std::string_view show_resource) {
    if (show_resource == "demo" || show_resource.empty()) {
        const std::filesystem::path demo_path = module_dir / "assets" / "scenes" / "demo.scene.json";
        if (std::filesystem::exists(demo_path)) {
            return genesis::SceneSerializer::load(demo_path);
        }
        return std::nullopt;
    }

    const std::filesystem::path resource_path(show_resource);
    std::filesystem::path absolute = module_dir / "assets" / resource_path;
    if (!std::filesystem::exists(absolute)) {
        absolute = module_dir / resource_path;
    }
    if (!std::filesystem::exists(absolute)) {
        return std::nullopt;
    }

    if (absolute.extension() == ".gns") {
        const genesis::GnsPackage package = genesis::GnsPackage::load(absolute);
        return package.resolveScene(absolute.parent_path());
    }

    const std::string ext = absolute.extension().string();
    if (ext == ".json" || ext == ".scene" || ext == ".scene.json") {
        return genesis::SceneSerializer::load(absolute);
    }

    return std::nullopt;
}

const genesis::SceneMaterial* findMaterial(const genesis::SceneDocument& document,
                                             const std::string& name) {
    for (const auto& [material_name, material] : document.materials) {
        if (material_name == name) {
            return &material;
        }
    }
    return nullptr;
}

CatalogActor actorFromNode(const genesis::SceneDocument& document,
                           const genesis::SceneDocumentNode& node) {
    CatalogActor actor{};
    actor.path = node.path;
    actor.group_label = friendlyNodeLabel(node.path);
    actor.param_id = parameterIdFromPath(node.path);
    actor.default_position = {
        node.local.position.x,
        node.local.position.y,
        node.local.position.z,
    };
    const glm::vec3 euler = glm::degrees(glm::eulerAngles(node.local.rotation));
    actor.default_rotation = {euler.x, euler.y, euler.z};
    actor.default_scale = {
        node.local.scale.x,
        node.local.scale.y,
        node.local.scale.z,
    };
    actor.default_visible = node.visible;

    if (node.material_ref.has_value()) {
        if (const genesis::SceneMaterial* material = findMaterial(document, *node.material_ref)) {
            actor.default_color = {
                material->base_color.r,
                material->base_color.g,
                material->base_color.b,
                material->base_color.a,
            };
        }
    }

    return actor;
}

CatalogLight lightFromNode(const genesis::SceneDocumentNode& node) {
    CatalogLight light{};
    light.path = node.path;
    light.group_label = friendlyNodeLabel(node.path);
    light.param_id = parameterIdFromPath(node.path);
    if (node.light.has_value()) {
        light.default_intensity = node.light->intensity;
    }
    return light;
}

} // namespace

std::string parameterIdFromPath(std::string_view node_path) {
    std::string id;
    id.reserve(node_path.size());
    for (const char character : node_path) {
        if (std::isalnum(static_cast<unsigned char>(character))) {
            id.push_back(character);
        } else {
            id.push_back('_');
        }
    }
    return id;
}

SceneParameterCatalog buildSceneParameterCatalog(const std::filesystem::path& module_dir) {
    SceneParameterCatalog catalog;
    std::map<std::string, CatalogActor> actors;
    std::map<std::string, CatalogLight> lights;

    const std::vector<std::string> shows = scanShowResources(module_dir);
    for (const std::string& show : shows) {
        std::optional<genesis::SceneDocument> document = loadDocumentForShow(module_dir, show);
        if (!document.has_value()) {
            continue;
        }

        for (const genesis::SceneDocumentNode& node : document->nodes) {
            if (node.mesh_ref.has_value() && !actors.contains(node.path)) {
                actors.emplace(node.path, actorFromNode(*document, node));
            }
            if (node.light.has_value() && !lights.contains(node.path)) {
                lights.emplace(node.path, lightFromNode(node));
            }
        }
    }

    catalog.actors.reserve(actors.size());
    for (auto& [path, actor] : actors) {
        (void)path;
        catalog.actors.push_back(std::move(actor));
    }
    std::sort(catalog.actors.begin(), catalog.actors.end(),
              [](const CatalogActor& a, const CatalogActor& b) { return a.path < b.path; });

    catalog.lights.reserve(lights.size());
    for (auto& [path, light] : lights) {
        (void)path;
        catalog.lights.push_back(std::move(light));
    }
    std::sort(catalog.lights.begin(), catalog.lights.end(),
              [](const CatalogLight& a, const CatalogLight& b) { return a.path < b.path; });

    return catalog;
}

} // namespace rxext::genesis_ext
