#include "runtime/RuntimePaths.hpp"

#include "platform/Paths.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

namespace genesis {

namespace {

std::string trim(std::string_view text) {
    std::string out(text);
    while (!out.empty() && std::isspace(static_cast<unsigned char>(out.front()))) {
        out.erase(out.begin());
    }
    while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back()))) {
        out.pop_back();
    }
    return out;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool pathExists(const std::filesystem::path& path) {
    return !path.empty() && std::filesystem::exists(path);
}

bool isDocumentPath(const std::filesystem::path& path) {
    const std::string ext = path.extension().string();
    return ext == ".json" || ext == ".scene" || ext == ".scene.json";
}

bool isGnsPath(const std::filesystem::path& path) {
    const std::string ext = path.extension().string();
    return equalsIgnoreCase(ext, ".gns") || equalsIgnoreCase(ext, ".genesis");
}

bool isGltfPath(const std::filesystem::path& path) {
    const std::string ext = path.extension().string();
    return ext == ".gltf" || ext == ".glb";
}

// Pixera clip dropdown: packaged shows only (.gns / .genesis), like Notch .dfx.
// Authoring .scene.json / glTF stay loadable by explicit resource string, but are
// not listed until expose-from-Genesis lands.
bool isPackagedShowFile(const std::filesystem::path& path) {
    return isGnsPath(path);
}

std::optional<std::filesystem::path> tryResolvePath(const std::filesystem::path& candidate) {
    if (pathExists(candidate)) {
        return std::filesystem::absolute(candidate);
    }
    return std::nullopt;
}

RuntimeSceneResource classifyPath(const std::filesystem::path& path) {
    if (isGnsPath(path)) {
        return {RuntimeSceneKind::Gns, path};
    }
    if (isDocumentPath(path)) {
        return {RuntimeSceneKind::Document, path};
    }
    return {RuntimeSceneKind::Gltf, path};
}

void appendUnique(std::vector<std::string>& paths, const std::string& path) {
    if (path.empty()) {
        return;
    }
    if (std::find(paths.begin(), paths.end(), path) == paths.end()) {
        paths.push_back(path);
    }
}

void scanShowDirectory(const std::filesystem::path& directory,
                       const std::filesystem::path& relative_root,
                       std::vector<std::string>& shows) {
    if (!std::filesystem::exists(directory)) {
        return;
    }

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file() && isPackagedShowFile(entry.path())) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    for (const std::filesystem::path& file : files) {
        const std::filesystem::path relative = std::filesystem::relative(file, relative_root);
        appendUnique(shows, relative.generic_string());
    }
}

} // namespace

void setRuntimeAssetRoots(const std::filesystem::path& module_dir) {
    if (module_dir.empty()) {
        clearRuntimeAssetRoots();
        return;
    }

    const std::vector<std::filesystem::path> roots = {
        module_dir,
        module_dir / "Assets",
        module_dir / "assets",
        module_dir / "Scenes",
        module_dir / "scenes",
    };
    setAssetSearchRoots(roots);
}

void clearRuntimeAssetRoots() {
    clearAssetSearchRoots();
}

std::optional<RuntimeSceneResource> resolveRuntimeSceneResource(
    const std::filesystem::path& module_dir, std::string_view resource,
    const std::function<std::string(std::string_view)>& resolve_storage) {
    const std::string trimmed = trim(resource);
    if (trimmed.empty() || equalsIgnoreCase(trimmed, "demo") || equalsIgnoreCase(trimmed, "default") ||
        equalsIgnoreCase(trimmed, "builtin")) {
        return RuntimeSceneResource{RuntimeSceneKind::Demo, {}};
    }

    const std::filesystem::path requested(resource);
    if (requested.is_absolute()) {
        if (auto resolved = tryResolvePath(requested)) {
            return classifyPath(*resolved);
        }
    }

    if (resolve_storage) {
        const std::string storage_path = resolve_storage(resource);
        if (!storage_path.empty()) {
            if (auto resolved = tryResolvePath(std::filesystem::path(storage_path))) {
                return classifyPath(*resolved);
            }
        }
    }

    if (!module_dir.empty()) {
        const std::filesystem::path candidates[] = {
            module_dir / "Scenes" / resource,
            module_dir / "scenes" / resource,
            module_dir / "assets" / "scenes" / resource,
            module_dir / "Assets" / "Scenes" / resource,
            module_dir / "assets" / resource,
            module_dir / "Assets" / resource,
            module_dir / resource,
        };
        for (const std::filesystem::path& candidate : candidates) {
            if (auto resolved = tryResolvePath(candidate)) {
                return classifyPath(*resolved);
            }
        }
    }

    try {
        const std::filesystem::path resolved = resolveAssetPath(std::filesystem::path(resource));
        return classifyPath(resolved);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<std::string> scanRuntimeShowResources(const std::filesystem::path& module_dir) {
    std::vector<std::string> shows;
    if (module_dir.empty()) {
        shows.push_back("default");
        return shows;
    }

    // Notch-like scene picker sources next to ExtGenesis.dll:
    //   scenes/            (preferred project drop folder)
    //   assets/scenes/     (bundled packaged shows shipped with the module)
    // Always store paths relative to the module directory so resolve matches.
    scanShowDirectory(module_dir / "scenes", module_dir, shows);
    scanShowDirectory(module_dir / "Scenes", module_dir, shows);
    scanShowDirectory(module_dir / "assets" / "scenes", module_dir, shows);
    scanShowDirectory(module_dir / "Assets" / "Scenes", module_dir, shows);

    if (shows.empty()) {
        shows.push_back("default");
    }
    return shows;
}

} // namespace genesis
