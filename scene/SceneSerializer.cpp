#include "scene/SceneSerializer.hpp"

#include "scene/ScenePath.hpp"
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace genesis {

namespace {

using json = nlohmann::json;

const char* lightTypeName(SceneLightType type) {
    switch (type) {
    case SceneLightType::Point:
        return "point";
    case SceneLightType::Spot:
        return "spot";
    default:
        return "directional";
    }
}

SceneLightType lightTypeFromName(const std::string& name) {
    if (name == "point") return SceneLightType::Point;
    if (name == "spot") return SceneLightType::Spot;
    return SceneLightType::Directional;
}

const char* alphaModeName(AlphaMode mode) {
    switch (mode) {
    case AlphaMode::Mask:
        return "mask";
    case AlphaMode::Blend:
        return "blend";
    default:
        return "opaque";
    }
}

AlphaMode alphaModeFromName(const std::string& name) {
    if (name == "mask") return AlphaMode::Mask;
    if (name == "blend") return AlphaMode::Blend;
    return AlphaMode::Opaque;
}

json vec3ToJson(const glm::vec3& v) {
    return json::array({v.x, v.y, v.z});
}

json vec4ToJson(const glm::vec4& v) {
    return json::array({v.x, v.y, v.z, v.w});
}

glm::vec3 vec3FromJson(const json& j) {
    if (!j.is_array() || j.size() < 3) {
        throw std::runtime_error("SceneSerializer: expected vec3 array");
    }
    return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
}

glm::vec4 vec4FromJson(const json& j) {
    if (!j.is_array() || j.size() < 4) {
        throw std::runtime_error("SceneSerializer: expected vec4 array");
    }
    return glm::vec4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());
}

json transformToJson(const Transform& t) {
    return json{
        {"position", vec3ToJson(t.position)},
        {"rotation", json::array({t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w})},
        {"scale", vec3ToJson(t.scale)},
    };
}

Transform transformFromJson(const json& j) {
    Transform t{};
    if (j.contains("position")) t.position = vec3FromJson(j["position"]);
    if (j.contains("scale")) t.scale = vec3FromJson(j["scale"]);
    if (j.contains("rotation")) {
        const json& r = j["rotation"];
        if (!r.is_array() || r.size() < 4) {
            throw std::runtime_error("SceneSerializer: expected rotation quaternion array");
        }
        t.rotation = glm::quat(r[3].get<float>(), r[0].get<float>(), r[1].get<float>(), r[2].get<float>());
    }
    return t;
}

json materialToJson(const std::string& name, const SceneMaterial& material) {
    json j{
        {"name", name},
        {"base_color", vec4ToJson(material.base_color)},
        {"metallic", material.metallic},
        {"roughness", material.roughness},
        {"emissive", vec3ToJson(material.emissive)},
        {"emissive_strength", material.emissive_strength},
        {"alpha_mode", alphaModeName(material.alpha_mode)},
        {"alpha_cutoff", material.alpha_cutoff},
    };
    if (!material.base_color_texture.empty()) j["base_color_texture"] = material.base_color_texture;
    if (!material.metallic_roughness_texture.empty()) {
        j["metallic_roughness_texture"] = material.metallic_roughness_texture;
    }
    if (!material.normal_texture.empty()) j["normal_texture"] = material.normal_texture;
    if (!material.emissive_texture.empty()) j["emissive_texture"] = material.emissive_texture;
    return j;
}

SceneMaterial materialFromJson(const json& j) {
    SceneMaterial material{};
    if (j.contains("base_color")) material.base_color = vec4FromJson(j["base_color"]);
    if (j.contains("metallic")) material.metallic = j["metallic"].get<float>();
    if (j.contains("roughness")) material.roughness = j["roughness"].get<float>();
    if (j.contains("emissive")) material.emissive = vec3FromJson(j["emissive"]);
    if (j.contains("emissive_strength")) material.emissive_strength = j["emissive_strength"].get<float>();
    if (j.contains("alpha_mode")) material.alpha_mode = alphaModeFromName(j["alpha_mode"].get<std::string>());
    if (j.contains("alpha_cutoff")) material.alpha_cutoff = j["alpha_cutoff"].get<float>();
    if (j.contains("base_color_texture")) material.base_color_texture = j["base_color_texture"].get<std::string>();
    if (j.contains("metallic_roughness_texture")) {
        material.metallic_roughness_texture = j["metallic_roughness_texture"].get<std::string>();
    }
    if (j.contains("normal_texture")) material.normal_texture = j["normal_texture"].get<std::string>();
    if (j.contains("emissive_texture")) material.emissive_texture = j["emissive_texture"].get<std::string>();
    return material;
}

json cameraToJson(const SceneCamera& camera) {
    return json{
        {"fov_y_degrees", glm::degrees(camera.fov_y_radians)},
        {"near", camera.near_plane},
        {"far", camera.far_plane},
    };
}

SceneCamera cameraFromJson(const json& j) {
    SceneCamera camera{};
    if (j.contains("fov_y_degrees")) {
        camera.fov_y_radians = glm::radians(j["fov_y_degrees"].get<float>());
    } else if (j.contains("fov_y_radians")) {
        camera.fov_y_radians = j["fov_y_radians"].get<float>();
    }
    if (j.contains("near")) camera.near_plane = j["near"].get<float>();
    if (j.contains("far")) camera.far_plane = j["far"].get<float>();
    return camera;
}

json lightToJson(const SceneLight& light) {
    return json{
        {"type", lightTypeName(light.type)},
        {"color", vec3ToJson(light.color)},
        {"intensity", light.intensity},
        {"enabled", light.enabled},
        {"range", light.range},
        {"inner_cone_degrees", glm::degrees(light.inner_cone_radians)},
        {"outer_cone_degrees", glm::degrees(light.outer_cone_radians)},
    };
}

SceneLight lightFromJson(const json& j) {
    SceneLight light{};
    if (j.contains("type")) light.type = lightTypeFromName(j["type"].get<std::string>());
    if (j.contains("color")) light.color = vec3FromJson(j["color"]);
    if (j.contains("intensity")) light.intensity = j["intensity"].get<float>();
    if (j.contains("enabled")) light.enabled = j["enabled"].get<bool>();
    if (j.contains("range")) light.range = j["range"].get<float>();
    if (j.contains("inner_cone_degrees")) {
        light.inner_cone_radians = glm::radians(j["inner_cone_degrees"].get<float>());
    } else if (j.contains("inner_cone_radians")) {
        light.inner_cone_radians = j["inner_cone_radians"].get<float>();
    }
    if (j.contains("outer_cone_degrees")) {
        light.outer_cone_radians = glm::radians(j["outer_cone_degrees"].get<float>());
    } else if (j.contains("outer_cone_radians")) {
        light.outer_cone_radians = j["outer_cone_radians"].get<float>();
    }
    return light;
}

} // namespace

using json = nlohmann::json;

SceneDocument SceneSerializer::buildDocument(const Scene& scene, const std::string& source,
                                             const std::vector<std::string>& mesh_names,
                                             const std::vector<std::string>& material_names,
                                             const std::vector<SceneMaterial>& materials) {
    SceneDocument document{};
    document.version = 1;
    document.source = source;

    if (scene.active_output_camera.has_value()) {
        document.active_output_camera = nodePath(scene, *scene.active_output_camera);
    }

    document.materials.reserve(material_names.size());
    for (std::size_t i = 0; i < material_names.size(); ++i) {
        SceneMaterial material{};
        if (i < materials.size()) material = materials[i];
        document.materials.emplace_back(material_names[i], material);
    }

    for (const Node& node : scene.nodes) {
        SceneDocumentNode entry{};
        entry.path = nodePath(scene, node.id);
        entry.name = node.name;
        if (node.parent.has_value()) entry.parent_path = nodePath(scene, *node.parent);
        entry.local = node.local;
        entry.visible = node.visible;
        if (node.mesh_resource.has_value() && *node.mesh_resource < mesh_names.size()) {
            entry.mesh_ref = mesh_names[*node.mesh_resource];
        }
        if (node.material_resource.has_value() && *node.material_resource < material_names.size()) {
            entry.material_ref = material_names[*node.material_resource];
        }
        entry.camera = node.camera;
        entry.light = node.light;
        document.nodes.push_back(std::move(entry));
    }

    return document;
}

void SceneSerializer::save(const SceneDocument& document, const std::filesystem::path& path) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Failed to open scene file for writing: " + path.string());
    }
    out << documentToJson(document).dump(2);
    spdlog::info("Saved scene document: {}", path.string());
}

json SceneSerializer::documentToJson(const SceneDocument& document) {
    json root{
        {"version", document.version},
        {"source", document.source},
    };

    if (document.active_output_camera.has_value()) {
        root["active_output_camera"] = *document.active_output_camera;
    }

    json materials = json::array();
    for (const auto& [name, material] : document.materials) {
        materials.push_back(materialToJson(name, material));
    }
    root["materials"] = materials;

    json nodes = json::array();
    for (const SceneDocumentNode& node : document.nodes) {
        json entry{
            {"path", node.path},
            {"name", node.name},
            {"visible", node.visible},
            {"transform", transformToJson(node.local)},
        };
        if (node.parent_path.has_value()) {
            entry["parent"] = *node.parent_path;
        } else {
            entry["parent"] = nullptr;
        }
        if (node.mesh_ref.has_value()) entry["mesh"] = *node.mesh_ref;
        if (node.material_ref.has_value()) entry["material"] = *node.material_ref;
        if (node.camera.has_value()) entry["camera"] = cameraToJson(*node.camera);
        if (node.light.has_value()) entry["light"] = lightToJson(*node.light);
        nodes.push_back(std::move(entry));
    }
    root["nodes"] = nodes;
    return root;
}

SceneDocument SceneSerializer::load(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Failed to open scene file: " + path.string());
    }

    const json root = json::parse(in);
    SceneDocument document = loadFromJson(root);
    spdlog::info("Loaded scene document: {} (source={}, nodes={})", path.string(), document.source,
                 document.nodes.size());
    return document;
}

SceneDocument SceneSerializer::loadFromJson(const json& root) {
    SceneDocument document{};
    document.version = root.value("version", 1);
    document.source = root.value("source", std::string{});

    if (root.contains("active_output_camera") && !root["active_output_camera"].is_null()) {
        document.active_output_camera = root["active_output_camera"].get<std::string>();
    }

    if (root.contains("materials") && root["materials"].is_array()) {
        for (const json& entry : root["materials"]) {
            if (!entry.contains("name")) {
                throw std::runtime_error("SceneSerializer: material entry missing name");
            }
            const std::string name = entry["name"].get<std::string>();
            document.materials.emplace_back(name, materialFromJson(entry));
        }
    }

    if (root.contains("nodes") && root["nodes"].is_array()) {
        for (const json& entry : root["nodes"]) {
            SceneDocumentNode node{};
            node.path = entry.at("path").get<std::string>();
            node.name = entry.value("name", node.path);
            node.visible = entry.value("visible", true);
            if (entry.contains("transform")) node.local = transformFromJson(entry["transform"]);
            if (entry.contains("parent") && !entry["parent"].is_null()) {
                node.parent_path = entry["parent"].get<std::string>();
            }
            if (entry.contains("mesh") && !entry["mesh"].is_null()) {
                node.mesh_ref = entry["mesh"].get<std::string>();
            }
            if (entry.contains("material") && !entry["material"].is_null()) {
                node.material_ref = entry["material"].get<std::string>();
            }
            if (entry.contains("camera")) node.camera = cameraFromJson(entry["camera"]);
            if (entry.contains("light")) node.light = lightFromJson(entry["light"]);
            document.nodes.push_back(std::move(node));
        }
    }

    return document;
}

} // namespace genesis
