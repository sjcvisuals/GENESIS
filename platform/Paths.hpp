#pragma once

#include <filesystem>

namespace genesis {

// Resolves an asset path for manual launches (cwd may differ from repo root).
// Search order: as-given (absolute), cwd-relative, executable-relative.
std::filesystem::path resolveAssetPath(const std::filesystem::path& path);

} // namespace genesis
