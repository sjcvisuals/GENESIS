#pragma once

#include "scene/SceneDocument.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace genesis {

// PIXERA stream description embedded in a .gns show package.
struct GnsStreamDesc {
    std::string handle = "main";
    std::uint32_t resolution_x = 1920;
    std::uint32_t resolution_y = 1080;
    bool use_scene_camera = false;
};

// Default RenderBridge parameter bindings for a show.
struct GnsBindings {
    std::string target_node;
    std::string light_node;
};

struct GnsPixeraConfig {
    std::vector<GnsStreamDesc> streams;
    GnsBindings bindings;
};

// GENESIS show package (.gns) — importable compositing resource (like Notch .dfx).
struct GnsPackage {
    int version = 1;
    std::string name;

    // Inline scene document, or scene_ref relative to the .gns file.
    std::optional<SceneDocument> scene;
    std::optional<std::string> scene_ref;

    GnsPixeraConfig pixera;

    static GnsPackage load(const std::filesystem::path& path);
    static void save(const GnsPackage& package, const std::filesystem::path& path);

    // Resolve and return the scene document from inline data or scene_ref.
    SceneDocument resolveScene(const std::filesystem::path& package_dir) const;
};

} // namespace genesis
