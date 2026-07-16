#include "scene/GnsPackage.hpp"

#include "scene/SceneSerializer.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace genesis {

namespace {

using json = nlohmann::json;

GnsBindings bindingsFromJson(const json& j) {
    GnsBindings bindings;
    if (j.contains("target_node") && !j["target_node"].is_null()) {
        bindings.target_node = j["target_node"].get<std::string>();
    }
    if (j.contains("light_node") && !j["light_node"].is_null()) {
        bindings.light_node = j["light_node"].get<std::string>();
    }
    return bindings;
}

GnsStreamDesc streamFromJson(const json& j) {
    GnsStreamDesc stream;
    stream.handle = j.value("handle", std::string{"main"});
    stream.resolution_x = j.value("resolution_x", 1920u);
    stream.resolution_y = j.value("resolution_y", 1080u);
    stream.use_scene_camera = j.value("use_scene_camera", false);
    return stream;
}

json bindingsToJson(const GnsBindings& bindings) {
    json j = json::object();
    if (!bindings.target_node.empty()) {
        j["target_node"] = bindings.target_node;
    }
    if (!bindings.light_node.empty()) {
        j["light_node"] = bindings.light_node;
    }
    return j;
}

json streamToJson(const GnsStreamDesc& stream) {
    return json{
        {"handle", stream.handle},
        {"resolution_x", stream.resolution_x},
        {"resolution_y", stream.resolution_y},
        {"use_scene_camera", stream.use_scene_camera},
    };
}

} // namespace

GnsPackage GnsPackage::load(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Failed to open GNS package: " + path.string());
    }

    const json root = json::parse(in);
    const std::string format = root.value("format", std::string{});
    // .genesis is the editor-facing package name; .gns remains the PIXERA show format.
    if (format != "gns" && format != "genesis") {
        throw std::runtime_error("GENESIS package missing or invalid format field: " + path.string());
    }

    GnsPackage package{};
    package.version = root.value("version", 1);
    package.name = root.value("name", path.stem().string());

    if (root.contains("scene_ref") && !root["scene_ref"].is_null()) {
        package.scene_ref = root["scene_ref"].get<std::string>();
    } else if (root.contains("scene") && root["scene"].is_object()) {
        package.scene = SceneSerializer::loadFromJson(root["scene"]);
    } else {
        throw std::runtime_error("GENESIS package must contain scene or scene_ref: " + path.string());
    }

    if (root.contains("pixera") && root["pixera"].is_object()) {
        const json& pixera = root["pixera"];
        if (pixera.contains("streams") && pixera["streams"].is_array()) {
            for (const json& entry : pixera["streams"]) {
                package.pixera.streams.push_back(streamFromJson(entry));
            }
        }
        if (pixera.contains("bindings") && pixera["bindings"].is_object()) {
            package.pixera.bindings = bindingsFromJson(pixera["bindings"]);
        }
    }

    if (package.pixera.streams.empty()) {
        package.pixera.streams.push_back(GnsStreamDesc{});
    }

    spdlog::info("Loaded GENESIS package: {} ({})", path.string(), package.name);
    return package;
}

void GnsPackage::save(const GnsPackage& package, const std::filesystem::path& path) {
    const std::string ext = path.extension().string();
    const bool genesis_ext = ext == ".genesis" || ext == ".GENESIS";
    json root{
        {"format", genesis_ext ? "genesis" : "gns"},
        {"version", package.version},
        {"name", package.name},
        {"pixera",
         json{
             {"streams", json::array()},
             {"bindings", bindingsToJson(package.pixera.bindings)},
         }},
    };

    if (package.scene_ref.has_value()) {
        root["scene_ref"] = *package.scene_ref;
    } else if (package.scene.has_value()) {
        root["scene"] = SceneSerializer::documentToJson(*package.scene);
    } else {
        throw std::runtime_error("GENESIS package must contain scene or scene_ref to save");
    }

    for (const GnsStreamDesc& stream : package.pixera.streams) {
        root["pixera"]["streams"].push_back(streamToJson(stream));
    }

    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Failed to open GENESIS package for writing: " + path.string());
    }
    out << root.dump(2);
    spdlog::info("Saved GENESIS package: {}", path.string());
}

SceneDocument GnsPackage::resolveScene(const std::filesystem::path& package_dir) const {
    if (scene.has_value()) {
        return *scene;
    }
    if (!scene_ref.has_value() || scene_ref->empty()) {
        throw std::runtime_error("GNS package has no scene content");
    }
    return SceneSerializer::load(package_dir / *scene_ref);
}

} // namespace genesis
