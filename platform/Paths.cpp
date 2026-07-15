#include "platform/Paths.hpp"

#include <SDL3/SDL.h>

#include <stdexcept>
#include <string>

namespace genesis {

namespace {

bool pathExists(const std::filesystem::path& path) {
    return !path.empty() && std::filesystem::exists(path);
}

} // namespace

std::filesystem::path resolveAssetPath(const std::filesystem::path& path) {
    if (path.empty()) {
        throw std::runtime_error("resolveAssetPath: empty path");
    }

    if (path.is_absolute()) {
        if (pathExists(path)) return path;
        throw std::runtime_error("Asset not found: " + path.string());
    }

    if (pathExists(path)) {
        return std::filesystem::absolute(path);
    }

    if (const char* base = SDL_GetBasePath()) {
        const std::filesystem::path exe_dir = base;
        SDL_free(const_cast<char*>(base));

        const std::filesystem::path from_exe = exe_dir / path;
        if (pathExists(from_exe)) {
            return std::filesystem::absolute(from_exe);
        }
    }

    throw std::runtime_error("Asset not found: " + path.string() +
                             " (cwd=" + std::filesystem::current_path().string() + ")");
}

} // namespace genesis
