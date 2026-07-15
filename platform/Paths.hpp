#pragma once

#include <filesystem>
#include <span>
#include <vector>

namespace genesis {

// Optional search roots checked before cwd / executable directory.
// Used by RenderBridge extension and Genesis Player for module-relative assets.
void setAssetSearchRoots(std::span<const std::filesystem::path> roots);
void clearAssetSearchRoots();

// Resolves an asset path for manual launches (cwd may differ from repo root).
// Search order: absolute, configured roots, cwd-relative, executable-relative.
std::filesystem::path resolveAssetPath(const std::filesystem::path& path);

} // namespace genesis
