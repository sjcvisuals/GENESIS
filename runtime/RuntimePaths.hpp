#pragma once

#include "runtime/RuntimeScene.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace genesis {

// Configure module-relative asset search roots (extension / player module directory).
void setRuntimeAssetRoots(const std::filesystem::path& module_dir);

void clearRuntimeAssetRoots();

// Resolve a show/scene resource string relative to module_dir and optional host storage.
std::optional<RuntimeSceneResource> resolveRuntimeSceneResource(
    const std::filesystem::path& module_dir, std::string_view resource,
    const std::function<std::string(std::string_view)>& resolve_storage = {});

std::vector<std::string> scanRuntimeShowResources(const std::filesystem::path& module_dir);

} // namespace genesis
