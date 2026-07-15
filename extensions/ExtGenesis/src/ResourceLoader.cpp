#include "ResourceLoader.h"

#include <string>

namespace rxext::genesis_ext {

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
    return path.extension() == ".gns";
}

bool isGltfPath(const std::filesystem::path& path) {
    const std::string ext = path.extension().string();
    return ext == ".gltf" || ext == ".glb";
}

std::optional<std::filesystem::path> tryResolvePath(const std::filesystem::path& candidate) {
    if (pathExists(candidate)) {
        return std::filesystem::absolute(candidate);
    }
    return std::nullopt;
}

ResolvedSceneResource classifyPath(const std::filesystem::path& path) {
    if (isGnsPath(path)) {
        return {SceneResourceKind::Gns, path};
    }
    if (isDocumentPath(path)) {
        return {SceneResourceKind::Document, path};
    }
    return {SceneResourceKind::Gltf, path};
}

} // namespace

std::optional<ResolvedSceneResource> resolveSceneResource(HostContext& host,
                                                          std::string_view resource,
                                                          const std::filesystem::path& module_dir) {
    const std::string trimmed = trim(resource);
    if (trimmed.empty() || equalsIgnoreCase(trimmed, "demo") || equalsIgnoreCase(trimmed, "builtin")) {
        return ResolvedSceneResource{SceneResourceKind::Demo, {}};
    }

    const std::filesystem::path requested(resource);
    if (requested.is_absolute()) {
        if (auto resolved = tryResolvePath(requested)) {
            return classifyPath(*resolved);
        }
    }

    if (const string storage_path = host.resolve_storage_filename(resource); !storage_path.empty()) {
        const std::filesystem::path storage_resolved{std::string(storage_path)};
        if (auto resolved = tryResolvePath(storage_resolved)) {
            return classifyPath(*resolved);
        }
    }

    if (!module_dir.empty()) {
        if (auto resolved = tryResolvePath(module_dir / "assets" / resource)) {
            return classifyPath(*resolved);
        }
        if (auto resolved = tryResolvePath(module_dir / resource)) {
            return classifyPath(*resolved);
        }
    }

    if (auto resolved = tryResolvePath(std::filesystem::path(resource))) {
        return classifyPath(*resolved);
    }

    host.log_error(std::string("GENESIS scene resource not found: ") + std::string(resource));
    return std::nullopt;
}

} // namespace rxext::genesis_ext
