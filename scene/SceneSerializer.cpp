#include "scene/SceneSerializer.hpp"

#include "particles/FluidVolume.hpp"
#include "scene/ScenePath.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>

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
    case AlphaMode::Volume:
        return "volume";
    default:
        return "opaque";
    }
}

AlphaMode alphaModeFromName(const std::string& name) {
    std::string lower = name;
    for (char& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lower == "mask") return AlphaMode::Mask;
    if (lower == "blend") return AlphaMode::Blend;
    if (lower == "volume") return AlphaMode::Volume;
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
        {"shading_model", "OpenPBR"},
        {"base_weight", material.base_weight},
        {"base_color", vec4ToJson(material.base_color)},
        {"metallic", material.metallic},
        {"base_metalness", material.metallic},
        {"base_diffuse_roughness", material.base_diffuse_roughness},
        {"specular_weight", material.specular_weight},
        {"specular_color", vec3ToJson(material.specular_color)},
        {"roughness", material.roughness},
        {"specular_roughness", material.roughness},
        {"specular_ior", material.specular_ior},
        {"transmission_weight", material.transmission_weight},
        {"transmission_color", vec3ToJson(material.transmission_color)},
        {"transmission_depth", material.transmission_depth},
        {"transmission_scatter", vec3ToJson(material.transmission_scatter)},
        {"transmission_scatter_anisotropy", material.transmission_scatter_anisotropy},
        {"transmission_dispersion_scale", material.transmission_dispersion_scale},
        {"transmission_dispersion_abbe", material.transmission_dispersion_abbe},
        {"subsurface_weight", material.subsurface_weight},
        {"subsurface_color", vec3ToJson(material.subsurface_color)},
        {"subsurface_radius", material.subsurface_radius},
        {"subsurface_radius_scale", vec3ToJson(material.subsurface_radius_scale)},
        {"subsurface_scatter_anisotropy", material.subsurface_scatter_anisotropy},
        {"coat_weight", material.coat_weight},
        {"coat_color", vec3ToJson(material.coat_color)},
        {"coat_roughness", material.coat_roughness},
        {"coat_ior", material.coat_ior},
        {"coat_darkening", material.coat_darkening},
        {"fuzz_weight", material.fuzz_weight},
        {"fuzz_color", vec3ToJson(material.fuzz_color)},
        {"fuzz_roughness", material.fuzz_roughness},
        {"thin_film_weight", material.thin_film_weight},
        {"thin_film_thickness", material.thin_film_thickness},
        {"thin_film_ior", material.thin_film_ior},
        {"emissive", vec3ToJson(material.emissive)},
        {"emissive_strength", material.emissive_strength},
        {"thin_walled", material.thin_walled},
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
    if (j.contains("base_weight")) material.base_weight = j["base_weight"].get<float>();
    if (j.contains("base_color")) material.base_color = vec4FromJson(j["base_color"]);
    if (j.contains("base_metalness")) {
        material.metallic = j["base_metalness"].get<float>();
    } else if (j.contains("metallic")) {
        material.metallic = j["metallic"].get<float>();
    }
    if (j.contains("base_diffuse_roughness")) {
        material.base_diffuse_roughness = j["base_diffuse_roughness"].get<float>();
    }
    if (j.contains("specular_weight")) material.specular_weight = j["specular_weight"].get<float>();
    if (j.contains("specular_color")) material.specular_color = vec3FromJson(j["specular_color"]);
    if (j.contains("specular_roughness")) {
        material.roughness = j["specular_roughness"].get<float>();
    } else if (j.contains("roughness")) {
        material.roughness = j["roughness"].get<float>();
    }
    if (j.contains("specular_ior")) material.specular_ior = j["specular_ior"].get<float>();
    if (j.contains("transmission_weight")) {
        material.transmission_weight = j["transmission_weight"].get<float>();
    }
    if (j.contains("transmission_color")) {
        material.transmission_color = vec3FromJson(j["transmission_color"]);
    }
    if (j.contains("transmission_depth")) {
        material.transmission_depth = j["transmission_depth"].get<float>();
    }
    if (j.contains("transmission_scatter")) {
        material.transmission_scatter = vec3FromJson(j["transmission_scatter"]);
    }
    if (j.contains("transmission_scatter_anisotropy")) {
        material.transmission_scatter_anisotropy = j["transmission_scatter_anisotropy"].get<float>();
    }
    if (j.contains("transmission_dispersion_scale")) {
        material.transmission_dispersion_scale = j["transmission_dispersion_scale"].get<float>();
    }
    if (j.contains("transmission_dispersion_abbe")) {
        material.transmission_dispersion_abbe = j["transmission_dispersion_abbe"].get<float>();
    }
    if (j.contains("subsurface_weight")) material.subsurface_weight = j["subsurface_weight"].get<float>();
    if (j.contains("subsurface_color")) material.subsurface_color = vec3FromJson(j["subsurface_color"]);
    if (j.contains("subsurface_radius")) material.subsurface_radius = j["subsurface_radius"].get<float>();
    if (j.contains("subsurface_radius_scale")) {
        material.subsurface_radius_scale = vec3FromJson(j["subsurface_radius_scale"]);
    }
    if (j.contains("subsurface_scatter_anisotropy")) {
        material.subsurface_scatter_anisotropy = j["subsurface_scatter_anisotropy"].get<float>();
    }
    if (j.contains("coat_weight")) material.coat_weight = j["coat_weight"].get<float>();
    if (j.contains("coat_color")) material.coat_color = vec3FromJson(j["coat_color"]);
    if (j.contains("coat_roughness")) material.coat_roughness = j["coat_roughness"].get<float>();
    if (j.contains("coat_ior")) material.coat_ior = j["coat_ior"].get<float>();
    if (j.contains("coat_darkening")) material.coat_darkening = j["coat_darkening"].get<float>();
    if (j.contains("fuzz_weight")) material.fuzz_weight = j["fuzz_weight"].get<float>();
    if (j.contains("fuzz_color")) material.fuzz_color = vec3FromJson(j["fuzz_color"]);
    if (j.contains("fuzz_roughness")) material.fuzz_roughness = j["fuzz_roughness"].get<float>();
    if (j.contains("thin_film_weight")) material.thin_film_weight = j["thin_film_weight"].get<float>();
    if (j.contains("thin_film_thickness")) {
        material.thin_film_thickness = j["thin_film_thickness"].get<float>();
    }
    if (j.contains("thin_film_ior")) material.thin_film_ior = j["thin_film_ior"].get<float>();
    if (j.contains("emissive")) material.emissive = vec3FromJson(j["emissive"]);
    if (j.contains("emissive_strength")) material.emissive_strength = j["emissive_strength"].get<float>();
    if (j.contains("thin_walled")) material.thin_walled = j["thin_walled"].get<bool>();
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
    case ParticleSpawnShape::Ring:
        return "ring";
    default:
        return "point";
    }
}

ParticleSpawnShape spawnShapeFromName(const std::string& name) {
    if (name == "sphere") return ParticleSpawnShape::Sphere;
    if (name == "cone") return ParticleSpawnShape::Cone;
    if (name == "box") return ParticleSpawnShape::Box;
    if (name == "ring" || name == "cylinder") return ParticleSpawnShape::Ring;
    return ParticleSpawnShape::Point;
}

const char* renderMeshName(ParticleRenderMesh mesh) {
    switch (mesh) {
    case ParticleRenderMesh::Sphere:
        return "sphere";
    case ParticleRenderMesh::Sprite:
        return "sprite";
    case ParticleRenderMesh::None:
        return "none";
    default:
        return "cube";
    }
}

ParticleRenderMesh renderMeshFromName(const std::string& name) {
    if (name == "sphere") return ParticleRenderMesh::Sphere;
    if (name == "sprite" || name == "billboard") return ParticleRenderMesh::Sprite;
    if (name == "none") return ParticleRenderMesh::None;
    return ParticleRenderMesh::Cube;
}

const char* velocityModeName(ParticleVelocityMode mode) {
    switch (mode) {
    case ParticleVelocityMode::FromPoint:
        return "from_point";
    case ParticleVelocityMode::Random:
        return "random";
    case ParticleVelocityMode::HorizontalPlane:
        return "horizontal_plane";
    default:
        return "cone";
    }
}

ParticleVelocityMode velocityModeFromName(const std::string& name) {
    if (name == "from_point") return ParticleVelocityMode::FromPoint;
    if (name == "random") return ParticleVelocityMode::Random;
    if (name == "horizontal_plane") return ParticleVelocityMode::HorizontalPlane;
    return ParticleVelocityMode::Cone;
}

json curve1ToJson(const ParticleCurve1& c) {
    json keys = json::array();
    for (std::uint8_t i = 0; i < c.count && i < ParticleCurve1::kMaxKeys; ++i) {
        keys.push_back({{"t", c.keys[i].t}, {"v", c.keys[i].v}});
    }
    return {{"keys", keys}};
}

ParticleCurve1 curve1FromJson(const json& j) {
    ParticleCurve1 c{};
    if (!j.contains("keys") || !j["keys"].is_array()) {
        return c;
    }
    for (const json& key : j["keys"]) {
        if (c.count >= ParticleCurve1::kMaxKeys) {
            break;
        }
        c.keys[c.count].t = key.value("t", 0.0f);
        c.keys[c.count].v = key.value("v", 1.0f);
        ++c.count;
    }
    return c;
}

json curveColorToJson(const ParticleCurveColor& c) {
    json keys = json::array();
    for (std::uint8_t i = 0; i < c.count && i < ParticleCurveColor::kMaxKeys; ++i) {
        keys.push_back({{"t", c.keys[i].t}, {"v", vec4ToJson(c.keys[i].v)}});
    }
    return {{"keys", keys}};
}

ParticleCurveColor curveColorFromJson(const json& j) {
    ParticleCurveColor c{};
    if (!j.contains("keys") || !j["keys"].is_array()) {
        return c;
    }
    for (const json& key : j["keys"]) {
        if (c.count >= ParticleCurveColor::kMaxKeys) {
            break;
        }
        c.keys[c.count].t = key.value("t", 0.0f);
        if (key.contains("v")) {
            c.keys[c.count].v = vec4FromJson(key["v"]);
        }
        ++c.count;
    }
    return c;
}

json particleEmitterToJson(const ParticleEmitter& e) {
    json bursts = json::array();
    for (std::uint8_t i = 0; i < e.burst_entries && i < e.bursts.size(); ++i) {
        bursts.push_back({{"time", e.bursts[i].time_seconds}, {"count", e.bursts[i].count}});
    }
    return json{
        {"enabled", e.enabled},
        {"looping", e.looping},
        {"world_space", e.world_space},
        {"duration", e.duration_seconds},
        {"start_delay", e.start_delay_seconds},
        {"mod_spawn_rate", e.mod_spawn_rate},
        {"rate", e.rate_per_second},
        {"burst", e.burst_count},
        {"bursts", bursts},
        {"max_particles", e.max_particles},
        {"mod_lifetime", e.mod_lifetime},
        {"mod_velocity", e.mod_velocity},
        {"mod_size", e.mod_size},
        {"mod_color", e.mod_color},
        {"lifetime_min", e.lifetime_min},
        {"lifetime_max", e.lifetime_max},
        {"speed_min", e.speed_min},
        {"speed_max", e.speed_max},
        {"size_min", e.size_min},
        {"size_max", e.size_max},
        {"size_end_multiplier", e.size_end_multiplier},
        {"color_start", vec4ToJson(e.color_start)},
        {"color_end", vec4ToJson(e.color_end)},
        {"velocity_mode", velocityModeName(e.velocity_mode)},
        {"inherit_velocity", e.inherit_velocity},
        {"mod_shape", e.mod_shape},
        {"force_gravity", e.force_gravity},
        {"gravity", vec3ToJson(e.gravity)},
        {"force_drag", e.force_drag},
        {"drag", e.drag},
        {"force_point", e.force_point},
        {"point_force_position", vec3ToJson(e.point_force_position)},
        {"point_force_strength", e.point_force_strength},
        {"point_force_softness", e.point_force_softness},
        {"force_vortex", e.force_vortex},
        {"vortex_strength", e.vortex_strength},
        {"vortex_axis", vec3ToJson(e.vortex_axis)},
        {"vortex_radius", e.vortex_radius},
        {"force_noise", e.force_noise},
        {"noise_strength", e.noise_strength},
        {"noise_frequency", e.noise_frequency},
        {"force_accel", e.force_accel},
        {"acceleration", vec3ToJson(e.acceleration)},
        {"kill_plane", e.kill_plane},
        {"kill_plane_y", e.kill_plane_y},
        {"kill_box", e.kill_box},
        {"kill_box_min", vec3ToJson(e.kill_box_min)},
        {"kill_box_max", vec3ToJson(e.kill_box_max)},
        {"mod_size_over_life", e.mod_size_over_life},
        {"mod_color_over_life", e.mod_color_over_life},
        {"size_over_life", curve1ToJson(e.size_over_life)},
        {"color_over_life", curveColorToJson(e.color_over_life)},
        {"scale_by_speed", e.scale_by_speed},
        {"scale_by_speed_range_min", e.scale_by_speed_range_min},
        {"scale_by_speed_range_max", e.scale_by_speed_range_max},
        {"scale_by_speed_mul_min", e.scale_by_speed_mul_min},
        {"scale_by_speed_mul_max", e.scale_by_speed_mul_max},
        {"spawn_per_unit", e.spawn_per_unit},
        {"spawn_shape", spawnShapeName(e.spawn_shape)},
        {"spawn_radius", e.spawn_radius},
        {"spawn_box_height", e.spawn_box_height},
        {"cone_angle_degrees", e.cone_angle_degrees},
        {"mod_sprite", e.mod_sprite},
        {"mod_mesh", e.mod_mesh},
        {"render_mesh", renderMeshName(e.render_mesh)},
        {"sprite_aspect", e.sprite_aspect},
        {"velocity_stretch", e.velocity_stretch},
        {"osc_drive", e.osc_drive},
        {"trail_enabled", e.trail_enabled},
        {"trail_segments", e.trail_segments},
        {"trail_sample_distance", e.trail_sample_distance},
        {"trail_width", e.trail_width},
        {"trail_width_end", e.trail_width_end},
        {"trail_alpha", e.trail_alpha},
        {"trail_lifetime", e.trail_lifetime},
        {"module_stack", e.module_stack},
        {"module_enabled", e.module_enabled},
        {"unlit", e.unlit},
        {"additive", e.additive},
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
    e.mod_spawn_rate = j.value("mod_spawn_rate", e.mod_spawn_rate);
    e.rate_per_second = j.value("rate", e.rate_per_second);
    e.burst_count = j.value("burst", e.burst_count);
    e.max_particles = j.value("max_particles", e.max_particles);
    e.mod_lifetime = j.value("mod_lifetime", e.mod_lifetime);
    e.mod_velocity = j.value("mod_velocity", e.mod_velocity);
    e.mod_size = j.value("mod_size", e.mod_size);
    e.mod_color = j.value("mod_color", e.mod_color);
    e.lifetime_min = j.value("lifetime_min", e.lifetime_min);
    e.lifetime_max = j.value("lifetime_max", e.lifetime_max);
    e.speed_min = j.value("speed_min", e.speed_min);
    e.speed_max = j.value("speed_max", e.speed_max);
    e.size_min = j.value("size_min", e.size_min);
    e.size_max = j.value("size_max", e.size_max);
    e.size_end_multiplier = j.value("size_end_multiplier", e.size_end_multiplier);
    if (j.contains("color_start")) e.color_start = vec4FromJson(j["color_start"]);
    if (j.contains("color_end")) e.color_end = vec4FromJson(j["color_end"]);
    if (j.contains("velocity_mode")) e.velocity_mode = velocityModeFromName(j["velocity_mode"].get<std::string>());
    e.inherit_velocity = j.value("inherit_velocity", e.inherit_velocity);
    e.mod_shape = j.value("mod_shape", e.mod_shape);
    e.force_gravity = j.value("force_gravity", e.force_gravity);
    if (j.contains("gravity")) e.gravity = vec3FromJson(j["gravity"]);
    e.force_drag = j.value("force_drag", e.force_drag);
    e.drag = j.value("drag", e.drag);
    e.force_point = j.value("force_point", e.force_point);
    if (j.contains("point_force_position")) e.point_force_position = vec3FromJson(j["point_force_position"]);
    e.point_force_strength = j.value("point_force_strength", e.point_force_strength);
    e.point_force_softness = j.value("point_force_softness", e.point_force_softness);
    e.force_vortex = j.value("force_vortex", e.force_vortex);
    e.vortex_strength = j.value("vortex_strength", e.vortex_strength);
    if (j.contains("vortex_axis")) e.vortex_axis = vec3FromJson(j["vortex_axis"]);
    e.vortex_radius = j.value("vortex_radius", e.vortex_radius);
    e.force_noise = j.value("force_noise", e.force_noise);
    e.noise_strength = j.value("noise_strength", e.noise_strength);
    e.noise_frequency = j.value("noise_frequency", e.noise_frequency);
    e.force_accel = j.value("force_accel", e.force_accel);
    if (j.contains("acceleration")) e.acceleration = vec3FromJson(j["acceleration"]);
    e.kill_plane = j.value("kill_plane", e.kill_plane);
    e.kill_plane_y = j.value("kill_plane_y", e.kill_plane_y);
    e.kill_box = j.value("kill_box", e.kill_box);
    if (j.contains("kill_box_min")) e.kill_box_min = vec3FromJson(j["kill_box_min"]);
    if (j.contains("kill_box_max")) e.kill_box_max = vec3FromJson(j["kill_box_max"]);
    e.mod_size_over_life = j.value("mod_size_over_life", e.mod_size_over_life);
    e.mod_color_over_life = j.value("mod_color_over_life", e.mod_color_over_life);
    if (j.contains("size_over_life")) e.size_over_life = curve1FromJson(j["size_over_life"]);
    if (j.contains("color_over_life")) e.color_over_life = curveColorFromJson(j["color_over_life"]);
    e.scale_by_speed = j.value("scale_by_speed", e.scale_by_speed);
    e.scale_by_speed_range_min = j.value("scale_by_speed_range_min", e.scale_by_speed_range_min);
    e.scale_by_speed_range_max = j.value("scale_by_speed_range_max", e.scale_by_speed_range_max);
    e.scale_by_speed_mul_min = j.value("scale_by_speed_mul_min", e.scale_by_speed_mul_min);
    e.scale_by_speed_mul_max = j.value("scale_by_speed_mul_max", e.scale_by_speed_mul_max);
    e.spawn_per_unit = j.value("spawn_per_unit", e.spawn_per_unit);
    if (j.contains("spawn_shape")) e.spawn_shape = spawnShapeFromName(j["spawn_shape"].get<std::string>());
    e.spawn_radius = j.value("spawn_radius", e.spawn_radius);
    e.spawn_box_height = j.value("spawn_box_height", e.spawn_box_height);
    e.cone_angle_degrees = j.value("cone_angle_degrees", e.cone_angle_degrees);
    e.mod_sprite = j.value("mod_sprite", e.mod_sprite);
    if (j.contains("render_mesh")) {
        e.render_mesh = renderMeshFromName(j["render_mesh"].get<std::string>());
    }
    if (j.contains("mod_mesh")) {
        e.mod_mesh = j.value("mod_mesh", e.mod_mesh);
    } else if (!e.mod_sprite && (e.render_mesh == ParticleRenderMesh::Sphere ||
                                 e.render_mesh == ParticleRenderMesh::Cube)) {
        // Legacy: disabling Sprite silently fell back to Sphere/Cube draw.
        e.mod_mesh = true;
    }
    e.sprite_aspect = j.value("sprite_aspect", e.sprite_aspect);
    e.velocity_stretch = j.value("velocity_stretch", e.velocity_stretch);
    e.osc_drive = j.value("osc_drive", e.osc_drive);
    e.trail_enabled = j.value("trail_enabled", e.trail_enabled);
    e.trail_segments = static_cast<std::uint8_t>(
        std::clamp(j.value("trail_segments", static_cast<int>(e.trail_segments)), 2, 24));
    e.trail_sample_distance = j.value("trail_sample_distance", e.trail_sample_distance);
    e.trail_width = j.value("trail_width", e.trail_width);
    e.trail_width_end = j.value("trail_width_end", e.trail_width_end);
    e.trail_alpha = j.value("trail_alpha", e.trail_alpha);
    e.trail_lifetime = j.value("trail_lifetime", e.trail_lifetime);
    e.unlit = j.value("unlit", e.unlit);
    e.additive = j.value("additive", e.additive);
    e.seed_override = j.value("seed_override", e.seed_override);
    e.fluid_coupled = j.value("fluid_coupled", e.fluid_coupled);
    if (j.contains("bursts") && j["bursts"].is_array()) {
        e.burst_entries = 0;
        for (const json& burst : j["bursts"]) {
            if (e.burst_entries >= e.bursts.size()) {
                break;
            }
            e.bursts[e.burst_entries].time_seconds = burst.value("time", 0.0f);
            e.bursts[e.burst_entries].count = burst.value("count", 0u);
            ++e.burst_entries;
        }
    }
    if (j.contains("module_stack")) {
        e.module_stack = j.value("module_stack", e.module_stack);
        e.module_enabled = j.value("module_enabled", e.module_stack);
        syncOptionalStackFlags(e);
    } else {
        // Legacy scenes: rebuild optional stack from classic enable flags.
        e.module_stack = 0;
        e.module_enabled = 0;
        inferOptionalModuleStack(e);
    }
    syncEmitterCurvesFromEndpoints(e);
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
        {"source_uvw", {f.source_uvw.x, f.source_uvw.y, f.source_uvw.z}},
        {"source_radius", f.source_radius},
        {"source_density", f.source_density},
        {"source_velocity", {f.source_velocity.x, f.source_velocity.y, f.source_velocity.z}},
        {"source2_enabled", f.source2_enabled},
        {"source2_uvw", {f.source2_uvw.x, f.source2_uvw.y, f.source2_uvw.z}},
        {"source2_radius", f.source2_radius},
        {"source2_density", f.source2_density},
        {"source2_velocity", {f.source2_velocity.x, f.source2_velocity.y, f.source2_velocity.z}},
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
    if (j.contains("source_uvw") && j["source_uvw"].is_array() && j["source_uvw"].size() >= 3) {
        f.source_uvw.x = j["source_uvw"][0];
        f.source_uvw.y = j["source_uvw"][1];
        f.source_uvw.z = j["source_uvw"][2];
    } else if (j.contains("source_uv") && j["source_uv"].is_array() && j["source_uv"].size() >= 2) {
        f.source_uvw.x = j["source_uv"][0];
        f.source_uvw.y = j["source_uv"][1];
        f.source_uvw.z = 0.5f;
    }
    f.source_radius = j.value("source_radius", f.source_radius);
    f.source_density = j.value("source_density", f.source_density);
    if (j.contains("source_velocity") && j["source_velocity"].is_array() &&
        j["source_velocity"].size() >= 2) {
        f.source_velocity.x = j["source_velocity"][0];
        f.source_velocity.y = j["source_velocity"][1];
        f.source_velocity.z = j["source_velocity"].size() >= 3 ? static_cast<float>(j["source_velocity"][2]) : 0.0f;
    }
    f.source2_enabled = j.value("source2_enabled", f.source2_enabled);
    if (j.contains("source2_uvw") && j["source2_uvw"].is_array() && j["source2_uvw"].size() >= 3) {
        f.source2_uvw.x = j["source2_uvw"][0];
        f.source2_uvw.y = j["source2_uvw"][1];
        f.source2_uvw.z = j["source2_uvw"][2];
    } else if (j.contains("source2_uv") && j["source2_uv"].is_array() && j["source2_uv"].size() >= 2) {
        f.source2_uvw.x = j["source2_uv"][0];
        f.source2_uvw.y = j["source2_uv"][1];
        f.source2_uvw.z = 0.5f;
    }
    f.source2_radius = j.value("source2_radius", f.source2_radius);
    f.source2_density = j.value("source2_density", f.source2_density);
    if (j.contains("source2_velocity") && j["source2_velocity"].is_array() &&
        j["source2_velocity"].size() >= 2) {
        f.source2_velocity.x = j["source2_velocity"][0];
        f.source2_velocity.y = j["source2_velocity"][1];
        f.source2_velocity.z =
            j["source2_velocity"].size() >= 3 ? static_cast<float>(j["source2_velocity"][2]) : 0.0f;
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
        entry.hidden_in_game = node.hidden_in_game;
        entry.collision_enabled = node.collision_enabled;
        entry.is_particle_system = node.is_particle_system;
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
            {"hidden_in_game", node.hidden_in_game},
            {"collision_enabled", node.collision_enabled},
            {"transform", transformToJson(node.local)},
        };
        if (node.is_particle_system) {
            entry["particle_system"] = true;
        }
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
            node.hidden_in_game = entry.value("hidden_in_game", false);
            node.collision_enabled = entry.value("collision_enabled", true);
            node.is_particle_system = entry.value("particle_system", false);
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
