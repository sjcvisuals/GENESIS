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

// True when path exists but is a Git LFS pointer text file (not the real blob).
// Real ONNX models are multi-MB; LFS pointers are ~100–200 byte ASCII stubs.
bool isGitLfsPointerFile(const std::filesystem::path& path);

// SPIR-V shaders for renderer pipelines. Search order: compile-time path (if it
// exists), module/exe-relative shaders/spv, then configured asset search roots.
std::filesystem::path resolveShaderDirectory();

} // namespace genesis
