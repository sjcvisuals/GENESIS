#include "scene/SceneSerializer.hpp"

#include "particles/FluidVolume.hpp"
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
        {"normal_scale", material.normal_scale},
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
    if (j.contains("normal_scale")) material.normal_scale = j["normal_scale"].get<float>();
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

const char* spawnShapeName(ParticleSpawnShape shape) {
    switch (shape) {
    case ParticleSpawnShape::Sphere:
        return "sphere";
    case ParticleSpawnShape::Cone:
        return "cone";
    case ParticleSpawnShape::Box:
        return "box";
    default:
        return "point";
    }
}

ParticleSpawnShape spawnShapeFromName(const std::string& name) {
    if (name == "sphere") return ParticleSpawnShape::Sphere;
    if (name == "cone") return ParticleSpawnShape::Cone;
    if (name == "box") return ParticleSpawnShape::Box;
    return ParticleSpawnShape::Point;
}

json particleEmitterToJson(const ParticleEmitter& e) {
    return json{
        {"enabled", e.enabled},
        {"looping", e.looping},
        {"world_space", e.world_space},
        {"duration", e.duration_seconds},
        {"start_delay", e.start_delay_seconds},
        {"rate", e.rate_per_second},
        {"burst", e.burst_count},
        {"max_particles", e.max_particles},
        {"lifetime_min", e.lifetime_min},
        {"lifetime_max", e.lifetime_max},
        {"speed_min", e.speed_min},
        {"speed_max", e.speed_max},
        {"size_min", e.size_min},
        {"size_max", e.size_max},
        {"size_end_multiplier", e.size_end_multiplier},
        {"color_start", vec4ToJson(e.color_start)},
        {"color_end", vec4ToJson(e.color_end)},
        {"gravity", vec3ToJson(e.gravity)},
        {"drag", e.drag},
        {"spawn_shape", spawnShapeName(e.spawn_shape)},
        {"spawn_radius", e.spawn_radius},
        {"spawn_box_height", e.spawn_box_height},
        {"cone_angle_degrees", e.cone_angle_degrees},
        {"render_mesh", e.render_mesh == ParticleRenderMesh::Sphere ? "sphere" : "cube"},
        {"unlit", e.unlit},
        {"seed_override", e.seed_override},
        {"fluid_coupled", e.fluid_coupled},
    };
}

ParticleEmitter particleEmitterFromJson(const json& j) {
    ParticleEmitter e = makeDefaultParticleEmitter();
    e.enabled = j.value("enabled", e.enabled);
    e.looping = j.value("looping", e.looping);
    e.world_space = j.value("world_space", e.world_space);
    e.duration_seconds = j.value("duration", e.duration_seconds);
    e.start_delay_seconds = j.value("start_delay", e.start_delay_seconds);
    e.rate_per_second = j.value("rate", e.rate_per_second);
    e.burst_count = j.value("burst", e.burst_count);
    e.max_particles = j.value("max_particles", e.max_particles);
    e.lifetime_min = j.value("lifetime_min", e.lifetime_min);
    e.lifetime_max = j.value("lifetime_max", e.lifetime_max);
    e.speed_min = j.value("speed_min", e.speed_min);
    e.speed_max = j.value("speed_max", e.speed_max);
    e.size_min = j.value("size_min", e.size_min);
    e.size_max = j.value("size_max", e.size_max);
    e.size_end_multiplier = j.value("size_end_multiplier", e.size_end_multiplier);
    if (j.contains("color_start")) e.color_start = vec4FromJson(j["color_start"]);
    if (j.contains("color_end")) e.color_end = vec4FromJson(j["color_end"]);
    if (j.contains("gravity")) e.gravity = vec3FromJson(j["gravity"]);
    e.drag = j.value("drag", e.drag);
    if (j.contains("spawn_shape")) e.spawn_shape = spawnShapeFromName(j["spawn_shape"].get<std::string>());
    e.spawn_radius = j.value("spawn_radius", e.spawn_radius);
    e.spawn_box_height = j.value("spawn_box_height", e.spawn_box_height);
    e.cone_angle_degrees = j.value("cone_angle_degrees", e.cone_angle_degrees);
    if (j.value("render_mesh", std::string{"cube"}) == "sphere") {
        e.render_mesh = ParticleRenderMesh::Sphere;
    }
    e.unlit = j.value("unlit", e.unlit);
    e.seed_override = j.value("seed_override", e.seed_override);
    e.fluid_coupled = j.value("fluid_coupled", e.fluid_coupled);
    return e;
}

json fluidVolumeToJson(const FluidVolume& f) {
    return json{
        {"enabled", f.enabled},
        {"grid_resolution", f.grid_resolution},
        {"extent_meters", {f.extent_meters.x, f.extent_meters.y, f.extent_meters.z}},
        {"viscosity", f.viscosity},
        {"vorticity_strength", f.vorticity_strength},
        {"velocity_dissipation", f.velocity_dissipation},
        {"density_dissipation", f.density_dissipation},
        {"source_enabled", f.source_enabled},
        {"source_uv", {f.source_uv.x, f.source_uv.y}},
        {"source_radius", f.source_radius},
        {"source_density", f.source_density},
        {"source_velocity", {f.source_velocity.x, f.source_velocity.y}},
        {"source2_enabled", f.source2_enabled},
        {"source2_uv", {f.source2_uv.x, f.source2_uv.y}},
        {"source2_radius", f.source2_radius},
        {"source2_density", f.source2_density},
        {"source2_velocity", {f.source2_velocity.x, f.source2_velocity.y}},
        {"animate_sources", f.animate_sources},
        {"dye_color", vec4ToJson(f.dye_color)},
        {"drive_particles", f.drive_particles},
        {"pressure_iterations", f.pressure_iterations},
    };
}

FluidVolume fluidVolumeFromJson(const json& j) {
    FluidVolume f = makeDefaultFluidVolume();
    f.enabled = j.value("enabled", f.enabled);
    f.grid_resolution = j.value("grid_resolution", f.grid_resolution);
    if (j.contains("extent_meters") && j["extent_meters"].is_array() && j["extent_meters"].size() >= 2) {
        f.extent_meters.x = j["extent_meters"][0];
        f.extent_meters.y = j["extent_meters"][1];
        // Legacy 2-component scenes: match depth to width for a square footprint.
        f.extent_meters.z = j["extent_meters"].size() >= 3 ? static_cast<float>(j["extent_meters"][2])
                                                           : f.extent_meters.x;
    }
    f.viscosity = j.value("viscosity", f.viscosity);
    f.vorticity_strength = j.value("vorticity_strength", f.vorticity_strength);
    f.velocity_dissipation = j.value("velocity_dissipation", f.velocity_dissipation);
    f.density_dissipation = j.value("density_dissipation", f.density_dissipation);
    f.source_enabled = j.value("source_enabled", f.source_enabled);
    if (j.contains("source_uv") && j["source_uv"].is_array() && j["source_uv"].size() >= 2) {
        f.source_uv.x = j["source_uv"][0];
        f.source_uv.y = j["source_uv"][1];
    }
    f.source_radius = j.value("source_radius", f.source_radius);
    f.source_density = j.value("source_density", f.source_density);
    if (j.contains("source_velocity") && j["source_velocity"].is_array() &&
        j["source_velocity"].size() >= 2) {
        f.source_velocity.x = j["source_velocity"][0];
        f.source_velocity.y = j["source_velocity"][1];
    }
    f.source2_enabled = j.value("source2_enabled", f.source2_enabled);
    if (j.contains("source2_uv") && j["source2_uv"].is_array() && j["source2_uv"].size() >= 2) {
        f.source2_uv.x = j["source2_uv"][0];
        f.source2_uv.y = j["source2_uv"][1];
    }
    f.source2_radius = j.value("source2_radius", f.source2_radius);
    f.source2_density = j.value("source2_density", f.source2_density);
    if (j.contains("source2_velocity") && j["source2_velocity"].is_array() &&
        j["source2_velocity"].size() >= 2) {
        f.source2_velocity.x = j["source2_velocity"][0];
        f.source2_velocity.y = j["source2_velocity"][1];
    }
    f.animate_sources = j.value("animate_sources", f.animate_sources);
    if (j.contains("dye_color")) f.dye_color = vec4FromJson(j["dye_color"]);
    f.drive_particles = j.value("drive_particles", f.drive_particles);
    f.pressure_iterations = j.value("pressure_iterations", f.pressure_iterations);
    return f;
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
        entry.pickable = node.pickable;
        if (node.mesh_resource.has_value() && *node.mesh_resource < mesh_names.size()) {
            entry.mesh_ref = mesh_names[*node.mesh_resource];
        }
        if (node.material_resource.has_value() && *node.material_resource < material_names.size()) {
            entry.material_ref = material_names[*node.material_resource];
        }
        entry.camera = node.camera;
        entry.light = node.light;
        entry.particle_emitter = node.particle_emitter;
        entry.particle_fluid = node.particle_fluid;
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
            {"pickable", node.pickable},
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
        if (node.particle_emitter.has_value()) {
            entry["particle_emitter"] = particleEmitterToJson(*node.particle_emitter);
        }
        if (node.particle_fluid.has_value()) {
            entry["particle_fluid"] = fluidVolumeToJson(*node.particle_fluid);
        }
        nodes.push_back(std::move(entry));
    }
    root["nodes"] = nodes;

    json animations = json::array();
    for (const AnimationClip& clip : document.animations) {
        animations.push_back(animationClipToJson(clip));
    }
    if (!animations.empty()) {
        root["animations"] = animations;
    }
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
            if (entry.contains("pickable")) {
                node.pickable = entry["pickable"].get<bool>();
            }
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
            if (entry.contains("particle_emitter")) {
                node.particle_emitter = particleEmitterFromJson(entry["particle_emitter"]);
            }
            if (entry.contains("particle_fluid")) {
                node.particle_fluid = fluidVolumeFromJson(entry["particle_fluid"]);
            }
            if (!entry.contains("pickable")) {
                node.pickable = !node.camera.has_value();
            }
            document.nodes.push_back(std::move(node));
        }
    }

    if (root.contains("animations") && root["animations"].is_array()) {
        for (const json& entry : root["animations"]) {
            document.animations.push_back(animationClipFromJson(entry));
        }
    }

    return document;
}

json SceneSerializer::animationClipToJson(const AnimationClip& clip) {
    json channels = json::array();
    for (const AnimationChannel& channel : clip.channels) {
        json ch{{"target", channel.target_node_name}};
        if (!channel.translations.empty()) {
            json keys = json::array();
            for (const AnimationVec3Key& key : channel.translations) {
                keys.push_back({{"t", key.time_seconds}, {"v", vec3ToJson(key.value)}});
            }
            ch["translations"] = keys;
        }
        if (!channel.rotations.empty()) {
            json keys = json::array();
            for (const AnimationQuatKey& key : channel.rotations) {
                keys.push_back({{"t", key.time_seconds},
                                {"v", json::array({key.value.x, key.value.y, key.value.z, key.value.w})}});
            }
            ch["rotations"] = keys;
        }
        if (!channel.scales.empty()) {
            json keys = json::array();
            for (const AnimationVec3Key& key : channel.scales) {
                keys.push_back({{"t", key.time_seconds}, {"v", vec3ToJson(key.value)}});
            }
            ch["scales"] = keys;
        }
        channels.push_back(std::move(ch));
    }
    return json{
        {"name", clip.name},
        {"duration", clip.duration_seconds},
        {"ticks_per_second", clip.ticks_per_second},
        {"channels", channels},
    };
}

AnimationClip SceneSerializer::animationClipFromJson(const json& j) {
    AnimationClip clip{};
    clip.name = j.value("name", std::string{"Clip"});
    clip.duration_seconds = j.value("duration", 0.0f);
    clip.ticks_per_second = j.value("ticks_per_second", 25.0f);
    if (!j.contains("channels") || !j["channels"].is_array()) {
        return clip;
    }
    for (const json& ch : j["channels"]) {
        AnimationChannel channel{};
        channel.target_node_name = ch.value("target", std::string{});
        if (ch.contains("translations") && ch["translations"].is_array()) {
            for (const json& key : ch["translations"]) {
                channel.translations.push_back(
                    AnimationVec3Key{key.value("t", 0.0f), vec3FromJson(key.at("v"))});
            }
        }
        if (ch.contains("rotations") && ch["rotations"].is_array()) {
            for (const json& key : ch["rotations"]) {
                const json& v = key.at("v");
                channel.rotations.push_back(AnimationQuatKey{
                    key.value("t", 0.0f),
                    glm::quat(v[3].get<float>(), v[0].get<float>(), v[1].get<float>(), v[2].get<float>()),
                });
            }
        }
        if (ch.contains("scales") && ch["scales"].is_array()) {
            for (const json& key : ch["scales"]) {
                channel.scales.push_back(
                    AnimationVec3Key{key.value("t", 0.0f), vec3FromJson(key.at("v"))});
            }
        }
        clip.channels.push_back(std::move(channel));
    }
    return clip;
}

} // namespace genesis
