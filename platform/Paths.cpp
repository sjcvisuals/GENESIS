#include "platform/Paths.hpp"

#include <SDL3/SDL.h>

#include <stdexcept>
#include <string>
#include <vector>
#include <optional>

namespace genesis {

namespace {

std::vector<std::filesystem::path> g_asset_search_roots;

bool pathExists(const std::filesystem::path& path) {
    return !path.empty() && std::filesystem::exists(path);
}

} // namespace

void setAssetSearchRoots(std::span<const std::filesystem::path> roots) {
    g_asset_search_roots.assign(roots.begin(), roots.end());
}

void clearAssetSearchRoots() {
    g_asset_search_roots.clear();
}

std::filesystem::path resolveAssetPath(const std::filesystem::path& path) {
    if (path.empty()) {
        throw std::runtime_error("resolveAssetPath: empty path");
    }

    if (path.is_absolute()) {
        if (pathExists(path)) return path;
        throw std::runtime_error("Asset not found: " + path.string());
    }

    for (const std::filesystem::path& root : g_asset_search_roots) {
        if (root.empty()) {
            continue;
        }
        const std::filesystem::path candidate = root / path;
        if (pathExists(candidate)) {
            return std::filesystem::absolute(candidate);
        }
    }

    if (const char* base = SDL_GetBasePath()) {
        const std::filesystem::path exe_dir = base;
        SDL_free(const_cast<char*>(base));

        const std::filesystem::path from_exe = exe_dir / path;
        if (pathExists(from_exe)) {
            return std::filesystem::absolute(from_exe);
        }
    }

    if (pathExists(path)) {
        return std::filesystem::absolute(path);
    }

    throw std::runtime_error("Asset not found: " + path.string() +
                             " (cwd=" + std::filesystem::current_path().string() + ")");
}

std::filesystem::path resolveShaderDirectory() {
    // Require a known mesh shader so empty or partial shader folders are rejected.
    const auto try_directory = [](const std::filesystem::path& directory) -> std::optional<std::filesystem::path> {
        if (!pathExists(directory)) {
            return std::nullopt;
        }
        if (!pathExists(directory / "mesh.vert.spv") || !pathExists(directory / "mesh.frag.spv")) {
            return std::nullopt;
        }
        return std::filesystem::absolute(directory);
    };

#ifdef GENESIS_SHADER_DIR
    if (const auto configured = try_directory(std::filesystem::path(GENESIS_SHADER_DIR))) {
        return *configured;
    }
#endif

    if (const char* base = SDL_GetBasePath()) {
        const std::filesystem::path exe_dir = base;
        SDL_free(const_cast<char*>(base));
        if (const auto resolved = try_directory(exe_dir / "shaders" / "spv")) {
            return *resolved;
        }
    }

    for (const std::filesystem::path& root : g_asset_search_roots) {
        if (root.empty()) {
            continue;
        }
        if (const auto resolved = try_directory(root / "shaders" / "spv")) {
            return *resolved;
        }
    }

    throw std::runtime_error("Shader directory not found (expected shaders/spv beside the executable or module)");
}

} // namespace genesis
