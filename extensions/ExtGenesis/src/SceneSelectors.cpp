#include "SceneSelectors.h"

#include "scene/ScenePath.hpp"

#include <algorithm>

namespace rxext::genesis_ext {

namespace {

bool isShowFile(const std::filesystem::path& path) {
    const std::string ext = path.extension().string();
    return ext == ".gns" || ext == ".json" || ext == ".scene" || ext == ".scene.json";
}

std::string friendlyShowLabel(const std::string& resource_path) {
    if (resource_path == "demo") {
        return "Built-in Demo";
    }
    const std::filesystem::path path(resource_path);
    return path.filename().string();
}

std::string friendlyNodeLabel(const std::string& node_path) {
    const std::size_t slash = node_path.find_last_of('/');
    if (slash == std::string::npos) {
        return node_path;
    }
    return node_path.substr(slash + 1) + " (" + node_path.substr(0, slash) + ")";
}

void appendUnique(std::vector<std::string>& paths, const std::string& path) {
    if (path.empty()) {
        return;
    }
    if (std::find(paths.begin(), paths.end(), path) == paths.end()) {
        paths.push_back(path);
    }
}

} // namespace

std::vector<std::string> scanShowResources(const std::filesystem::path& module_dir) {
    std::vector<std::string> shows{"demo"};

    const auto scan_dir = [&](const std::filesystem::path& directory) {
        if (!std::filesystem::exists(directory)) {
            return;
        }
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
            if (entry.is_regular_file() && isShowFile(entry.path())) {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());
        for (const std::filesystem::path& file : files) {
            const std::filesystem::path relative =
                std::filesystem::relative(file, module_dir / "assets");
            appendUnique(shows, relative.generic_string());
        }
    };

    if (!module_dir.empty()) {
        scan_dir(module_dir / "assets" / "scenes");
        scan_dir(module_dir / "assets");
    }

    return shows;
}

SceneSelectorLists buildSceneSelectorLists(const genesis::Scene& scene) {
    SceneSelectorLists lists;

    for (const genesis::Node& node : scene.nodes) {
        const std::string path = genesis::nodePath(scene, node.id);
        if (node.mesh_resource.has_value() || node.has_mesh) {
            appendUnique(lists.actor_paths, path);
        }
        if (node.light.has_value() || node.has_light) {
            appendUnique(lists.light_paths, path);
        }
    }

    std::sort(lists.actor_paths.begin(), lists.actor_paths.end());
    std::sort(lists.light_paths.begin(), lists.light_paths.end());
    return lists;
}

std::int32_t findSelectorIndex(const std::vector<std::string>& options, const std::string& value,
                               std::int32_t fallback) {
    if (options.empty()) {
        return 0;
    }
    const auto it = std::find(options.begin(), options.end(), value);
    if (it == options.end()) {
        return std::clamp(fallback, std::int32_t{0}, static_cast<std::int32_t>(options.size() - 1));
    }
    return static_cast<std::int32_t>(std::distance(options.begin(), it));
}

void setShowEnumOptions(ParameterInt* parameter, const std::vector<std::string>& show_paths,
                        std::int32_t selected_index) {
    if (!parameter || show_paths.empty()) {
        return;
    }

    std::vector<std::string> labels;
    labels.reserve(show_paths.size());
    for (const std::string& path : show_paths) {
        labels.push_back(friendlyShowLabel(path));
    }

    const std::int32_t clamped =
        std::clamp(selected_index, std::int32_t{0}, static_cast<std::int32_t>(show_paths.size() - 1));
    parameter->set_property<std::vector<std::string>>(PropertyNames::enum_names, std::move(labels));
    parameter->set_value(clamped);
}

void setNodeEnumOptions(ParameterInt* parameter, const std::vector<std::string>& node_paths,
                        std::int32_t selected_index) {
    if (!parameter || node_paths.empty()) {
        return;
    }

    std::vector<std::string> labels;
    labels.reserve(node_paths.size());
    for (const std::string& path : node_paths) {
        labels.push_back(friendlyNodeLabel(path));
    }

    const std::int32_t clamped =
        std::clamp(selected_index, std::int32_t{0}, static_cast<std::int32_t>(node_paths.size() - 1));
    parameter->set_property<std::vector<std::string>>(PropertyNames::enum_names, std::move(labels));
    parameter->set_value(clamped);
}

} // namespace rxext::genesis_ext
